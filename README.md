# Zapret GUI

GUI-приложение для обхода DPI-блокировок на основе [zapret](https://github.com/bol-van/zapret). Построено на Qt 6 / QML, работает на macOS, Windows и Linux.

## Что делает

Перехватывает исходящий трафик и модифицирует пакеты так, чтобы DPI-система провайдера не могла их распознать и заблокировать. Поддерживает TCP (HTTP/HTTPS) и UDP (QUIC, Discord voice).

**TCP** — прозрачный прокси tpws: split, disorder, OOB, TLS record splitting, hostcase
**UDP** — утилита udp-bypass (macOS): fake QUIC/STUN пакеты через raw socket + PF route-to
**Windows/Linux** — nfqws/winws: fake, multisplit, fakedsplit, seqovl

## Стратегии

Приложение поставляется с готовыми стратегиями:

| Стратегия | Платформы | Описание |
|-----------|-----------|----------|
| **General** | Windows, Linux, macOS | Рекомендуемая. Multisplit для TCP, fake для UDP |
| **General ALT11** | macOS | TCP+UDP для macOS. Disorder + fake QUIC 11 повторов |
| **ALT (Fake+FakeDSplit)** | Windows, Linux | Fake с fakedsplit и TCP timestamp fooling |
| **Simple Fake** | Windows, Linux | Только fake пакеты |
| **TPWS Full** | macOS | Disorder + hostcase + domcase + TLS record split |
| **TPWS Split + TLS Record** | macOS | Split на SNI extension + TLS record splitting |

Стратегии определены в `resources/strategies.json` — можно редактировать или добавлять свои.

## Сборка

### Зависимости

- CMake 3.21+
- Qt 6.5+ (Core, Quick, Network, Concurrent)
- C++17 компилятор

### macOS

```bash
cmake -B build -DCMAKE_PREFIX_PATH=$(brew --prefix qt6)
cmake --build build
```

Результат: `build/zapret-gui.app`

### Linux

```bash
cmake -B build
cmake --build build
```

### Windows

```bash
cmake -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64
cmake --build build --config Release
```

## Использование

1. Запустить приложение
2. Выбрать стратегию (General — для большинства случаев)
3. Нажать **Start**
4. При первом запуске появится запрос пароля администратора (один раз)

### macOS: автоматический passwordless sudo

При первом запуске приложение создает файл `/etc/sudoers.d/zapret` с NOPASSWD-правилами для используемых утилит. После этого пароль больше не запрашивается.

Удалить: `sudo rm /etc/sudoers.d/zapret`

## Настройки

- **Auto-start** — установка как системный сервис (launchd/systemd)
- **Game filter** — расширенный диапазон портов (1024-65535) для игрового трафика
- **IPSet mode** — фильтрация по IP-диапазонам в дополнение к доменным спискам
- **Тема** — светлая / темная / системная

## Структура проекта

```
src/
  core/           — движок, менеджеры стратегий, процессов, конфига
  models/         — модели данных для QML (лог, список стратегий)
  platform/       — платформенный код (macOS, Linux, Windows, Android, iOS)
qml/              — UI на QML (страницы, компоненты)
resources/
  bin/            — бинарники tpws/nfqws/winws для каждой платформы
  strategies.json — определения стратегий
  lua/            — Lua-скрипты автотестирования
lists/            — хостлисты и IP-сеты
fake/             — fake-пакеты для DPI bypass
tools/udp-bypass/ — исходник udp-bypass (macOS, C)
```

## Лицензия

Основано на проекте [zapret](https://github.com/bol-van/zapret) (MIT).
