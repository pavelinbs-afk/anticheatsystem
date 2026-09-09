# anticheat (CS2 MetaMod)

Серверный античит для CS2. Детекция в C++ (MetaMod), баны и репорты — через те же каналы, что AdminPlugin / PlaytimeReporter.

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

| Очки | Действие |
|------|----------|
| 15+ | мониторинг (только серверный лог) |
| 25+ | warn в лог |
| **35+** | **репорт** в модерацию (`css_anticheat_auto_report`) — **без кика** |
| **50+** | **бан 45 дней** через AdminPlugin (`css_anticheat_apply_ban`) → HTML по центру 10 сек → кик |

Очки подозрительности **никогда не показываются** игроку.

## Интеграция

- Бан → тот же `banned_steamids.json` + POST `/api/cs2/sanctions`, что и админка.
- Репорт → тот же ticket ingest, что `!report` (source `cs2_anticheat`).

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
    "integrity_checker": { "enabled": false }
  },
  "scoring": {
    "monitor_threshold": 15,
    "warn_threshold": 25,
    "report_threshold": 35,
    "ban_threshold": 50,
    "score_decay_per_minute": 1.2
  },
  "ban": {
    "duration_days": 45,
    "reason": "Использование читов",
    "countdown_seconds": 10
  },
  "notes": {
    "reports": "35+ -> css_anticheat_auto_report (PlaytimeReporter ticket API)",
    "bans": "50+ -> css_anticheat_apply_ban (AdminPlugin banned_steamids + /api/cs2/sanctions + 10s HTML)"
  }
}
```
