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
