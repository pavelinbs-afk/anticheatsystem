# anticheat (CS2 MetaMod)

Серверный античит для CS2 (MetaMod / KHook). Детекция в C++, баны, репорты и warn админам — через ServerCommand-мост в AdminPlugin / PlaytimeReporter. Проверка бана/IP на бэкенде — локальным curl.

## Сборка (WSL / Linux)

```bash
cd cs2-anticheat
bash build_cmake.sh
```

Артефакты: `build/addons/anticheat/` + `build/addons/metamod/anticheat.vdf`.

Нужны Docker (для совместимого GLIBC) или Ubuntu 20.04/22.04. Зависимости `deps/hl2sdk-cs2` и `deps/metamod-source` клонируются рядом в `pluginscpp/deps` (общие с CustomRounds/lr_core).

## Установка

1. Скопируйте `addons/anticheat` и `addons/metamod/anticheat.vdf` на сервер.
2. Убедитесь, что загружены **AdminPlugin** и **PlaytimeReporter** (CounterStrikeSharp).
3. Настройте `admin_freeze_sync.json` и `playtime_reporter.json` как обычно — баны/репорты уйдут туда же.

## Пороги

Дефолты из кода (переопределяются конфигом):

| Очки | Действие |
|------|----------|
| 22+ | мониторинг (только серверный лог) |
| 34+ | warn в лог |
| **45+** | **репорт** в модерацию (`css_anticheat_auto_report`) — **без кика** |
| **56+** | **HTML-предупреждение админам** (не бан) |
| **70+** | после admin-warn + corroboration → бан |
| **82 за <1 мин** после warn (`fast_ban_threshold` / `fast_ban_window_sec`) | **быстрый бан** (при 2+ каналах детекта) |

Очки подозрительности **никогда не показываются** игроку.

Отложенного бана (5–20 мин) нет. Репорт не банит; admin-warn — только уведомление. Автобан: **ban_threshold** при продолжающихся детектах, или **fast-ban** в течение `fast_ban_window_sec` после warn.

## Интеграция

- Бан → `css_anticheat_apply_ban` → AdminPlugin (`banned_steamids.json` + POST `/api/cs2/sanctions` с `source=anticheat`).
- Bypass/IP check → локальный curl (`BackendClient`) GET `/api/cs2/anticheat/check`; `enable_backend_check`, `backend_api_url`, `backend_api_token` (Bearer = `CS2_FREEZE_SERVICE_TOKEN` / `CS_PLUGIN_SECRET`). Активный бан на бэкенде → кик, IP-связь с баном → только репорт.
- Репорт → тот же ticket ingest, что `!report` (source `cs2_anticheat`).
- Консоль: `ac_status` — версия, git-сборка, интерфейсы, модули, backend, пороги. Подробный лог: `logging.enable_debug_log` в конфиге.
- **Anti-WH / Fog of War:** полноценный FoW (не слать врага за стеной/в тени, bake геометрии, latency preload) — отдельный MetaMod-класс плагинов вроде CS2FOW: хук `ISource2GameEntities::CheckTransmit` + BVH карты + smoke voxels. В нашем SDK `CCheckTransmitInfo` помечен как incomplete (нет надёжного `dont_transmit`), bake/trace worker нет — в **anticheat** сейчас только **детекция** WH (score), не скрытие позиций. Подключать anti-ESP отдельным плагином, не смешивать с scoring.

## Конфиг

Файл на сервере: `addons/anticheat/configs/anticheat_config.json` (пример из `config/anticheat_config.json`):

```json
{
  "plugin_name": "anticheat",
  "version": "1.0.0",
  "modules": {
    "aim_analyzer": { "enabled": true, "snap_angle_threshold": 55.0, "snap_time_ms": 50 },
    "wallhack_detector": { "enabled": false },
    "movement_analyzer": { "enabled": true, "bhop_detection": false, "speed_threshold": 380.0 },
    "statistics_tracker": { "enabled": true, "kd_threshold": 7.0, "headshot_pct_threshold": 85.0, "min_kills": 12 },
    "fps_drop_detector": { "enabled": true, "fps_max_frame_ms": 45.0, "fps_spike_stddev_ms": 20.0, "fps_min_spikes": 4, "enable_fps_drop_detection": true },
    "integrity_checker": { "enabled": false }
  },
  "scoring": {
    "monitor_threshold": 15,
    "warn_threshold": 25,
    "report_threshold": 35,
    "admin_warn_threshold": 50,
    "ban_threshold": 55,
    "fast_ban_threshold": 82,
    "fast_ban_window_sec": 60,
    "score_decay_per_minute": 1.2
  },
  "backend": { "enable_backend_check": true, "backend_api_url": "https://perfecteam.ru", "backend_api_token": "" },
  "ban": {
    "duration_days": 45,
    "reason": "Использование читов",
    "countdown_seconds": 10
  },
  "notes": {
    "reports": "35+ -> css_anticheat_auto_report (PlaytimeReporter ticket API)",
    "bans": "55+ continued after warn, or fast-ban 82 within 60s after warn -> css_anticheat_apply_ban"
  }
}
```
