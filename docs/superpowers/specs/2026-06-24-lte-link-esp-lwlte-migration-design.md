# LTE Link esp-lwlte Migration Design

## Goal

Migrate `lte_link` to the current esp-lwlte public API and make Wi-Fi/LTE
dual-mode switching work with a **hot-standby-network** strategy: LTE registers
to the network and keeps PDP active at boot (standing by in `DEGRADED`), and its
MQTT connection is brought up only while LTE is the active link. This yields
fast failover (~1-3 s MQTT bring-up, skipping the ~15-30 s registration+PDP
phase) while keeping LTE MQTT traffic off when Wi-Fi is healthy.

## Background: esp-lwlte broke `lte_link`

esp-lwlte is pulled in via `EXTRA_COMPONENT_DIRS = ../esp-lwlte/src` (project
root `CMakeLists.txt:3`), so any esp-lwlte change immediately affects this
project. After Smart_Socket's last LTE work (2026-05-28), esp-lwlte received a
chain of breaking refactors that make the current `lte_link` fail to compile
and semantically wrong even if it did:

| esp-lwlte commit | Impact on `lte_link` |
|---|---|
| `5ccc24c` rename opaque handles | `lwlte_t` -> `lwlte_handle_t` |
| `b546144` decouple MQTT client init | `mqtt_client` substruct removed from init config; MQTT now created via separate `lwlte_mqtt_init()` |
| `c165c19` replace callback slots with shared event bus | `lwlte_register_event_callback()` deleted; events now go over esp_event bases `LWLTE_EVENT` / `LWLTE_MQTT_EVENT` / `LWLTE_TCP_EVENT` |
| `1fc6e2b` / `0fb2bc8` group configs | config struct flattened -> nested `lwlte_base_config_t` (`.base.{uart,at_engine,modem,core,event}`) |
| `58535ad` symmetric stop lifecycle | `lwlte_connect/disconnect` -> `lwlte_start/stop`; `stop` now hard-powers-off via EN pin |

Concrete compile/logic failures in `lte_link.c`:
- `#include "lwlte_air780ep.h"` (line 26) — file no longer exists; everything
  moved into `lwlte.h`.
- `lwlte_t *` handle type renamed to `lwlte_handle_t *`.
- Flat config + `.mqtt_client` substruct no longer exists.
- MQTT client never created (no `lwlte_mqtt_init()` call) -> all MQTT calls fail.
- `lwlte_register_event_callback()` deleted; old single-callback + mixed event
  enum gone.
- `lwlte_connect/disconnect` renamed to `lwlte_start/stop`.
- MQTT DATA payload path `data->data.mqtt_msg.*` -> `data->msg.*`, and the DATA
  event now carries a heap buffer that **must** be released via
  `lwlte_mqtt_event_data_release()` (current code leaks it).

What is still valid (unchanged): `lwlte_state_t` / `lwlte_mqtt_state_t` enum
names and values; signatures of `lwlte_mqtt_publish/subscribe/unsubscribe`,
`lwlte_get_state`, `lwlte_mqtt_get_state`, `lwlte_destroy`; the pure-logic
subscription table and status mapping in `lte_link_internal.c`.

## Strategy Decision

**Hot-standby-network, MQTT-on-active.** Rationale: matches the architecture
doc's "LTE is a P0 capability" framing; gives fast failover without paying full
LTE-always-on MQTT traffic; the network/PDP phase (~15-30 s) is the expensive
part and is paid once at boot.

To let LTE start/stop MQTT on active transitions, a small `network_manager`
change is accepted (path B): a new **optional** op `set_active(bool)`, rather
than overloading existing `start`/`stop`. This keeps the lifecycle
(start/stop = full up/down) orthogonal to the active role (set_active =
engage/disengage MQTT) and leaves `wifi_link` untouched (zero regression risk).

## Design

### Section 1 — Ops contract and lifecycle/state model

#### 1.1 New optional op `set_active`

`network_link_priv.h` ops table gains one optional method:

```c
esp_err_t (*set_active)(network_link_t *me, bool active);
```

`network_link.h` / `network_link.c` gain a wrapper that treats a `NULL` impl as
a no-op returning `ESP_OK` (per `err.md` §5.3 optional-op convention):

```c
esp_err_t network_link_set_active(network_link_t *me, bool active) {
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "link is null");
    if (me->ops == NULL || me->ops->set_active == NULL) return ESP_OK;  /* optional, no-op */
    return me->ops->set_active(me, active);
}
```

- `wifi_link`: `ops->set_active = NULL` -> no-op, **zero changes**.
- `lte_link`: implements `set_active` to control MQTT.

#### 1.2 Orthogonal semantics: lifecycle vs role

| op | Meaning | WiFi impl | LTE impl |
|----|---------|-----------|----------|
| `start()` | bring link to "standby" | full connect -> READY | `lwlte_start()` (network only) -> DEGRADED |
| `stop()` | full power-down to IDLE | full disconnect | `lwlte_stop()` (stop MQTT + deactivate PDP + EN power-off) |
| `set_active(true)` | engage for active duty (aim READY) | no-op (already READY) | `lwlte_mqtt_start()` (async) |
| `set_active(false)` | disengage (back to standby) | no-op | `lwlte_mqtt_stop()` (keep network -> DEGRADED) |

The **standby state is the crux of the strategy**: LTE standby = network online,
MQTT off (`DEGRADED`). Failover only adds MQTT bring-up (~1-3 s).

#### 1.3 State machine

```
create()        start()                 set_active(true)        stop()
  |               |                        |                     |
  v               v                        v                     v
IDLE --> STARTING --> DEGRADED -------> (MQTT connecting) --> READY
(obj only)  (registering   (net online     set_active(true)     |   ^
            + PDP)         MQTT off)          ^                 |   | set_active(true)
                             |                |                 |   |
                             |                +-----------------+   |
                             | set_active(false)                   |
                             +-------------------------------------+
                                                          stop()   |
                                                                   v
                                                                 IDLE (powered off)
```

#### 1.4 Per-function responsibilities

Follows `architecture.md` §7.3 (create = objects only; start = register events
+ bring up).

- **`create()`**: `lwlte_air780ep_init()` (creates facade, does not probe
  hardware) + `lwlte_mqtt_init()` (creates MQTT object, no connection). No
  network, no event registration. Returns non-NULL even if hardware absent;
  absence degrades naturally via status.
- **`start()`**: register `LWLTE_EVENT` / `LWLTE_MQTT_EVENT` esp_event handlers
  (instance handles stored in struct) + async `lwlte_start()`. Idempotent.
  Target DEGRADED.
- **`set_active(true/false)`**: only `lwlte_mqtt_start/stop`. Calls lwlte
  **without** holding the lte_link mutex (FSM events are dispatched from the
  FSM task, not synchronously, but the rule avoids any re-entrancy). Updates an
  internal `mqtt_active` flag.
- **`stop()`**: `lwlte_stop()` (full power-off) + unregister esp_event handlers
  -> IDLE. Only triggered by `network_manager_stop` (full shutdown); never
  during operation.
- **`destroy()`**: set destroying -> drain `active_rx_callbacks` -> unregister
  handlers -> `lwlte_stop` -> `lwlte_destroy` -> free sub_table / strings /
  mutex / self.

#### 1.5 Async semantics

`lwlte_start` / `lwlte_mqtt_start` / `lwlte_mqtt_stop` are all async-submit
(ESP_OK = "request submitted", not "done"). `start()` / `set_active()` return
OK and state progresses via events + live `get_status()` polling. This fits
`network_manager`'s poll model (monitor every `failover_recheck_ms` = 5 s).

Failover timeline (Wi-Fi degrades -> switch to LTE):
1. monitor sees primary unusable, backup (LTE) DEGRADED usable -> `switch_active(LTE)`.
2. `set_active(LTE,true)` -> async `lwlte_mqtt_start` (~1-3 s to CONNECTED).
3. During the ~1-3 s window LTE is still DEGRADED; `publish` (which requires
   READY) returns `ESP_ERR_INVALID_STATE` for that active link -> the periodic
   metering publish drops at most one cycle. Acceptable.
4. `LWLTE_MQTT_EVENT_CONNECTED` -> LTE replays its own sub_table -> READY,
   publish resumes.

### Section 2 — `network_manager` changes (2 small spots)

#### 2.1 `network_manager_start()` — start both links + engage selected

Currently starts primary only (falls back to backup). Change to start both
(backup best-effort) and call `set_active(true)` on the selected link:

```c
primary_ret = network_link_start(me->primary);
if (primary_ret == ESP_OK) selected = me->primary;
else if (me->backup != NULL) { if (network_link_start(me->backup) == ESP_OK) selected = me->backup; }
if (selected == NULL) { /* existing failure handling */ }

me->active = selected;
me->failback_since_us = 0ULL;
(void)network_link_set_active(selected, true);          /* NEW: engage; WiFi=no-op, LTE=mqtt_start */

/* NEW: hot-standby — bring the non-selected link up too (best-effort) */
network_link_t *other = (selected == me->primary) ? me->backup : me->primary;
if (other != NULL) (void)network_link_start(other);     /* LTE -> DEGRADED; failure only warned */

(void)network_manager_replay_subscriptions_locked(me, selected);
/* existing start_monitor */
```

#### 2.2 `switch_active_locked()` — engage new, disengage old

```c
static void network_manager_switch_active_locked(network_manager_t *me, network_link_t *link) {
    if (me == NULL || link == NULL || me->active == link) return;
    network_link_t *old = me->active;
    me->active = link;
    me->failback_since_us = 0ULL;
    (void)network_link_set_active(link, true);           /* NEW */
    if (old != NULL) (void)network_link_set_active(old, false);  /* NEW */
    (void)network_manager_replay_subscriptions_locked(me, link);  /* existing */
}
```

#### 2.3 Unchanged (verified)

- `network_manager_stop()`: already calls `stop()` on both links; LTE stop =
  `lwlte_stop` (power off) is correct for full shutdown; no `set_active` needed.
- `monitor_once()`: its best-effort `start(backup)` / `start(primary)` remain
  valid (start = "ensure network standby", idempotent); MQTT engage/disengage is
  driven solely by `switch_active`. Switch decision logic (`usable = READY ||
  DEGRADED`) naturally treats LTE standby DEGRADED as a switchable target.

#### 2.4 Concurrency invariant (must preserve in new lte_link)

The current DATA path already does the right thing — **call `rx_cb` (the
network_manager bridge, which takes the manager mutex) only after releasing the
lte_link mutex**. This must be preserved, otherwise the DATA path (lte_link
mutex held -> manager mutex) and `replay_subscriptions` (manager mutex held ->
lte_link mutex via `network_link_subscribe`) form a lock-order inversion.

Unified lock order: **manager mutex -> lte_link mutex**, and the DATA callback
never holds the lte_link mutex when entering the manager. No cycle.

### Section 3 — Events, DATA path, config, edges, testing

#### 3.1 Event handling (default loop)

All Smart_Socket modules run on the **default** esp_event loop (`main.c:81`
creates it; `app_controller`'s `event_loop` is `NULL` so it uses the
else-branch default-loop registration; all `esp_event_post` are 4-arg =
default). esp-lwlte is configured with `.base.event.loop = NULL` (default), and
its `LWLTE_EVENT` / `LWLTE_MQTT_EVENT` / `LWLTE_TCP_EVENT` bases coexist with
the project's own bases on the same default loop. `lte_link` registers with
4-arg `esp_event_handler_instance_register` (default loop), consistent with the
rest of the project and with esp-lwlte's dispatch target.

`start()` registers two handlers (instance handles stored for clean unregister):

- **`LWLTE_EVENT` (net handler) — diagnostic only**: `NET_ONLINE/OFFLINE/ERROR`
  -> `ESP_LOGI/W`. Status is read live via `get_status()`; the handler exists
  only to make LTE bring-up observable.
- **`LWLTE_MQTT_EVENT` (mqtt handler) — functional**:
  - `CONNECTED`: `mqtt_connected=true`; replay LTE's own sub_table (clean_session
    loses subscriptions on reconnect, must re-subscribe).
  - `DISCONNECTED`: `mqtt_connected=false`.
  - `DATA`: forward to `rx_cb` (see 3.2).
  - `ERROR`: `ESP_LOGW` + `error_code`.
  - others: ignore.

#### 3.2 DATA path (memory contract — most critical)

```c
case LWLTE_MQTT_EVENT_DATA: {
    lwlte_mqtt_event_data_t *data = event_data;
    if (!data || !data->msg.topic || data->msg.topic_len == 0 ||
        data->msg.payload_len > (size_t)INT_MAX) {
        if (data) lwlte_mqtt_event_data_release(data);   /* release unconditionally */
        break;
    }
    network_rx_cb_t rx_cb = NULL; void *rx_ctx = NULL;
    if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) {
        if (!me->destroying && me->rx_cb) { rx_cb = me->rx_cb; rx_ctx = me->rx_ctx; me->active_rx_callbacks++; }
        xSemaphoreGive(me->mutex);
    }
    if (!rx_cb) { lwlte_mqtt_event_data_release(data); break; }

    char *topic = malloc(data->msg.topic_len + 1);       /* topic not 0-terminated */
    memcpy(topic, data->msg.topic, data->msg.topic_len);
    topic[data->msg.topic_len] = '\0';
    const network_rx_data_t rx = { .topic = topic,
                                   .data = (const char *)data->msg.payload,
                                   .data_len = (int)data->msg.payload_len };
    rx_cb(&rx, rx_ctx);                  /* INVARIANT: no lte mutex held entering manager */
    free(topic);
    /* dec active_rx_callbacks ... */
    lwlte_mqtt_event_data_release(data); /* single consumer: release exactly once, after rx_cb */
    break;
}
```

**Single-consumer holds:** LTE not-active -> MQTT stopped -> no DATA; even if
received defensively, the manager bridge filters by `source_link == active` and
drops; `lte_link` still releases. No leak, no double-free.

#### 3.3 Status mapping

`lte_link_internal_map_status` needs no logic change (enum names/values
unchanged): `ONLINE + MQTT STOPPED/WAITING_NET -> DEGRADED` (the standby
state), `ONLINE + MQTT CONNECTED -> READY`. `query_status` swaps the handle
type to `lwlte_handle_t *` and calls `lwlte_get_state` / `lwlte_mqtt_get_state`.

#### 3.4 Config field mapping (`lte_link_config_t` -> esp-lwlte nested)

| `lte_link_config_t` | esp-lwlte |
|---|---|
| `uart_num` / `tx_gpio` / `rx_gpio` / `baud_rate` | `base.uart.{num,tx_pin,rx_pin,baud_rate}` |
| `en_gpio` | `base.modem.en_pin` |
| `init_ready_timeout_ms` (**name kept**, avoids Kconfig churn) | `base.modem.ready_timeout_ms` |
| `net_activate_timeout_ms` | `base.core.net_activate_timeout_ms` |
| `apn` | `base.core.apn` |
| hardcoded `LTE_LINK_PRIMARY_CID = 1` | `base.core.primary_cid` |
| — | `base.event.loop = NULL` (default loop) |
| — | `base.at_engine = {0}`, `base.modem.reset_pulse_ms = 0` (defaults) |
| `mqtt_*` fields | `lwlte_mqtt_config_t` (separate `lwlte_mqtt_init`) |

Change: **remove `auto_connect`** (new init never connects; semantics gone).
Keep `mqtt_enabled`: when true, do `lwlte_mqtt_init` + allow `set_active` to
touch MQTT; when false, LTE is forever DEGRADED (this project is always true).

#### 3.5 Edge cases (each verified)

1. **LTE hardware absent**: `lwlte_air780ep_init` does not probe hardware ->
   create non-NULL; `lwlte_start` never reaches ONLINE -> state ERROR, manager
   never switches, silent degradation, no crash.
2. **`set_active(true)` before network online**: `lwlte_mqtt_start` async ->
   MQTT FSM `WAITING_NET` -> connects once network is up. Safe.
3. **Idempotent `set_active`**: repeated `mqtt_start` (already CONNECTED) /
   `mqtt_stop` (already STOPPED) return `ESP_ERR_INVALID_STATE` -> lte_link
   treats as OK (mirror existing `lte_link.c:450-458` handling).
4. **destroy ordering**: set destroying -> drain `active_rx_callbacks` ->
   unregister esp_event handlers -> `lwlte_stop` -> `lwlte_destroy` -> free
   sub_table/strings -> delete mutex -> free self. Handlers are unregistered
   before `lwlte_stop` so no callbacks fire into a half-torn-down object.
5. **Belt-and-suspenders**: `network_manager_destroy` clears the bridge rx_cb
   (`network_link_register_rx_cb(NULL)`) before link destroy, so even if a DATA
   event still fires, the bridge `rx_cb == NULL` drops it.

#### 3.6 Testing

- **Host tests (`test/host/test_lte_link_internal.c` + `test/support/lwlte.h`):
  no change needed.** They test pure logic (subscription table + `map_status`);
  the relevant enum names/values are unchanged, and the stub already matches.
  `lte_link_internal.c` is unchanged.
- **`lte_link.c` (init/events/lifecycle): hardware verification only** (needs
  real esp-lwlte + esp_event + UART). Hardware checklist:
  1. LTE registers after boot (`LWLTE_EVENT_NET_ONLINE`), reaches DEGRADED.
  2. Wi-Fi primary active, publishes telemetry; LTE standing by (no MQTT).
  3. Drop Wi-Fi (kill AP / wrong SSID) -> monitor switches to LTE ->
     `set_active` -> LTE MQTT connects -> READY -> telemetry resumes via LTE.
  4. Restore Wi-Fi -> failback after `failback_delay` -> switch to Wi-Fi ->
     LTE `set_active(false)` -> MQTT stops -> LTE back to DEGRADED.
  5. Downlink RPC over LTE -> `LWLTE_MQTT_EVENT_DATA` -> forwarded -> relay
     toggles.
  6. Subscriptions replayed on LTE MQTT connect (attributes / RPC topics).
  7. Unplug LTE module -> no crash, LTE stays ERROR, Wi-Fi unaffected.

## File Change Manifest

| File | Change |
|---|---|
| `main/network/network_link_priv.h` | add `set_active` to `network_link_ops_t` |
| `main/network/network_link.h` | declare `network_link_set_active()` |
| `main/network/network_link.c` | implement `network_link_set_active()` wrapper (NULL = no-op) |
| `main/network/lte/lte_link.h` | drop `auto_connect`; (rest of config unchanged) |
| `main/network/lte/lte_link.c` | full rewrite against new esp-lwlte API: new lifecycle (create/start/stop/destroy), `set_active` impl, esp_event migration, DATA+release path, nested config mapping, idempotent ops |
| `main/network/lte/lte_link_internal.c` | **unchanged** (pure logic) |
| `main/network/lte/lte_link_internal.h` | type tweak if needed (`lwlte_t` -> `lwlte_handle_t` in any forward decl; map_status signature uses enums, unaffected) |
| `main/network/network_manager.c` | 2 spots: `start()` starts both + engages selected; `switch_active_locked()` calls `set_active` on new/old |
| `main/network/wifi/wifi_link.c` | **unchanged** (`set_active = NULL`) |
| `main/main.c` | drop `.auto_connect` from the `lte_link_config_t` literal |
| `test/support/lwlte.h` | verify still matches (likely no change) |
| `test/host/test_lte_link_internal.c` | **unchanged** |

## Out of Scope

- TCP client / ping client of esp-lwlte (not used by Smart_Socket).
- esp-lwlte `lwlte_get_net_state()` finer granularity (status mapping stays on
  `lwlte_state_t` + `lwlte_mqtt_state_t`; can refine later).
- `LWLTE_MQTT_EVENT_SUBSCRIBED` confirmation-based replay (current fire-and-
  replay is sufficient; can harden later).
- Any change to `wifi_link` lifecycle.
- Custom event loop (everything stays on the default loop).
