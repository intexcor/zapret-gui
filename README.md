# Zapret GUI

GUI-приложение для обхода DPI-блокировок на основе [zapret](https://github.com/bol-van/zapret) и [zapret2](https://github.com/bol-van/zapret2). Построено на Qt 6 / QML, работает на **macOS**, **Windows**, **Linux**, **Android** и **iOS**.

## Что делает

Перехватывает исходящий трафик и модифицирует пакеты так, чтобы DPI-система провайдера не могла их распознать и заблокировать. Поддерживает TCP (HTTP/HTTPS) и UDP (QUIC, Discord voice).

### По платформам

| Платформа | TCP bypass | UDP/QUIC bypass |
|-----------|-----------|-----------------|
| **Windows** | winws: fake, multisplit, fakedsplit, seqovl | winws: fake QUIC/STUN |
| **Linux** | nfqws: fake, multisplit, fakedsplit, seqovl | nfqws: fake QUIC/STUN |
| **macOS** | tpws: split, disorder, OOB, TLS record, hostcase | udp-bypass: fake QUIC через raw socket + PF route-to |
| **Android** | VPN + JNI: TLS ClientHello split/disorder | VPN + JNI: fake QUIC с низким TTL |
| **iOS** | VPN + Swift: TLS ClientHello split/disorder | VPN + Swift: fake QUIC с низким TTL |

### Мобильная архитектура (Android / iOS)

На мобилках VPN-туннель захватывает весь трафик (TCP + UDP). Вместо внешнего tpws-процесса DPI bypass встроен прямо в VPN:

- **TCP**: relay через protected/bypass-tunnel сокеты + split первого TLS ClientHello
- **UDP**: relay + инъекция fake QUIC пакетов с низким TTL перед оригиналом
- Общая C-библиотека (`src/dpi/`) для парсинга пакетов и детекции QUIC/TLS

## Стратегии

Приложение поставляется с готовыми стратегиями:

| Стратегия | Платформы | Описание |
|-----------|-----------|----------|
| **General** | Windows, Linux, macOS | Рекомендуемая. Multisplit для TCP, fake для UDP |
| **General ALT11** | macOS | TCP+UDP для macOS. Disorder + fake QUIC 11 повторов |
| **Mobile Full** | Android, iOS | TCP split + QUIC fake. YouTube, Discord web и QUIC |
| **Mobile Discord** | Android, iOS | Discord voice + web + YouTube. QUIC fake + game ports |
| **ALT (Fake+FakeDSplit)** | Windows, Linux | Fake с fakedsplit и TCP timestamp fooling |
| **Simple Fake** | Windows, Linux | Только fake пакеты |
| **TPWS Full** | macOS, Android, iOS | Disorder + hostcase + domcase + TLS record split |
| **TPWS Split** | macOS, Android, iOS | TCP split — простой и эффективный |

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

### Android

```bash
cmake -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DCMAKE_PREFIX_PATH=$QT_ANDROID_PATH
cmake --build build-android
```

Собирается APK с `libvpn-processor.so` (JNI-библиотека для обработки пакетов).

### iOS

Собирается через Xcode. Packet Tunnel Extension (`ZapretPacketTunnel`) должен быть добавлен как отдельный таргет с `dpi_bypass.c` в compile sources и `DPIBypassBridge.h` как bridging header.

## Использование

1. Запустить приложение
2. Выбрать стратегию (General — десктоп, Mobile Full — мобилки)
3. Нажать **Start**
4. При первом запуске: пароль администратора (macOS/Linux) или разрешение VPN (Android/iOS)

### macOS: автоматический passwordless sudo

При первом запуске приложение создает файл `/etc/sudoers.d/zapret` с NOPASSWD-правилами для используемых утилит. После этого пароль больше не запрашивается.

Удалить: `sudo rm /etc/sudoers.d/zapret`

## Настройки

- **Auto-start** — установка как системный сервис (launchd/systemd)
- **Game filter** — расширенный диапазон портов для игрового трафика (Discord voice, STUN)
- **IPSet mode** — фильтрация по IP-диапазонам в дополнение к доменным спискам
- **Тема** — светлая / темная / системная

## Структура проекта

```
src/
  core/           — движок, менеджеры стратегий, процессов, конфига
  models/         — модели данных для QML (лог, список стратегий)
  platform/       — платформенный код (macOS, Linux, Windows, Android, iOS)
  dpi/            — общая C-библиотека: парсинг IP/TCP/UDP, детекция QUIC/TLS
qml/              — UI на QML (страницы, компоненты)
platform/
  android/jni/    — JNI пакетный процессор (vpn_processor, tcp_relay, udp_relay)
  android/src/    — ZapretVpnService.java
  ios/ZapretPacketTunnel/ — Swift пакетный процессор (PacketProcessor, TCPRelay, UDPRelay)
resources/
  bin/            — бинарники tpws/nfqws/winws/dvtws2 для десктопных платформ
  strategies.json — определения стратегий
  lua/            — Lua-скрипты автотестирования
lists/            — хостлисты и IP-сеты
fake/             — fake-пакеты для DPI bypass (.bin)
tools/udp-bypass/ — исходник udp-bypass (macOS, C)
```

## Бинарники

Десктопные бинарники в `resources/bin/`:

| Бинарник | Источник | Платформа |
|----------|----------|-----------|
| `tpws` | [zapret](https://github.com/bol-van/zapret) релизы | macOS |
| `nfqws` | [zapret](https://github.com/bol-van/zapret) релизы | Linux |
| `winws.exe` | [zapret](https://github.com/bol-van/zapret) релизы | Windows |
| `dvtws2`, `ip2net`, `mdig` | [zapret2](https://github.com/bol-van/zapret2) — компилируются из исходников | macOS |

На мобильных платформах внешние бинарники не нужны — DPI bypass встроен в VPN (JNI на Android, Swift на iOS).

## Лицензия

Основано на проекте [zapret](https://github.com/bol-van/zapret) (MIT).
