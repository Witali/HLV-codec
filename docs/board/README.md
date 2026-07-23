# Документация ESP32-2432S028 CYD2USB

Плата на фотографиях `docs/IMG_20260722_174354.jpg` и
`docs/IMG_20260722_174401.jpg` относится к двухпортовой ревизии
ESP32-2432S028: USB-C используется для питания, Micro-USB подключён к
USB-UART CH340C, а дисплей работает через контроллер ST7789. В каталоге
Sunton для PlatformIO эта комбинация обозначена как `ESP32-2432S028Rv3`.

## Локальные копии документов

| Файл | Содержание | Источник |
| --- | --- | --- |
| [ESP32-2432S028-schematic.pdf](ESP32-2432S028-schematic.pdf) | Объединённая двухстраничная схема ESP-2432S028 V0.1 | [Mischianti](https://mischianti.org/esp32-2432s028-cheap-yellow-display-high-resolution-pinout-datasheet-schema-and-specs/) |
| [ESP32-2432S028-schematic-MCU.jpg](ESP32-2432S028-schematic-MCU.jpg) | Питание, CH340C, автозагрузка, SD и периферия | [OriginalDocumentation/5-Schematic](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/tree/main/OriginalDocumentation/5-Schematic) |
| [ESP32-2432S028-schematic-LCM.jpg](ESP32-2432S028-schematic-LCM.jpg) | ESP32, дисплей, touch и усилитель | [OriginalDocumentation/5-Schematic](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/tree/main/OriginalDocumentation/5-Schematic) |
| [ESP32-2432S028-specification.pdf](ESP32-2432S028-specification.pdf) | Спецификация ESP32-2432S028R | [OriginalDocumentation/2-Specification](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/tree/main/OriginalDocumentation/2-Specification) |
| [ESP32-2432S028-user-manual.pdf](ESP32-2432S028-user-manual.pdf) | Исходное руководство производителя для Arduino | [OriginalDocumentation/6-User_Manual](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/tree/main/OriginalDocumentation/6-User_Manual) |

Объединённая схема ближе всего к имеющейся плате: на ней показаны CH340C,
двухтранзисторная цепь `DTR#/RTS#`, отдельные кнопки `BOOT` и `RST`, GPIO26
усилителя и посадочное место U4 под дополнительную flash-память. Однако
обозначения компонентов и разводка могут различаться между партиями, поэтому
перед пайкой соединения нужно проверять мультиметром.

Спецификация и руководство относятся к более ранней однопортовой ревизии
`ESP32-2432S028R` с ILI9341. Их распиновка большей части периферии полезна как
справочная, но сведения о USB-разъёмах, контроллере дисплея и его
инициализации нельзя переносить на двухпортовую плату без проверки.

Исходные документы в каталоге `OriginalDocumentation` сохранены со ссылками
на происхождение. Сопровождающий репозиторий отдельно предупреждает, что не
владеет правами для выдачи лицензии на эти архивные файлы.

## Репозитории платы

- [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) —
  основной общественный архив CYD: исходные документы, распиновка, примеры,
  настройка и аппаратные доработки.
- [rzeldent/platformio-espressif32-sunton](https://github.com/rzeldent/platformio-espressif32-sunton) —
  определения плат Sunton. Строка `ESP32-2432S028Rv3` соответствует нашей
  двухпортовой версии со ST7789.
- [rzeldent/esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay) —
  драйверы LVGL и низкоуровневые настройки для семейства дисплейных плат
  Sunton.
- [Обсуждение различий двухпортовой версии](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/issues/278) —
  подтверждение, что двухпортовая ESP32-2432S028 использует ST7789, тогда как
  распространённая ESP32-2432S028R — ILI9341.

## Справочные материалы для USB-загрузки

- [Espressif: Boot Mode Selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html)
- [Официальная схема ESP32-DevKitC V4](https://dl.espressif.com/dl/schematics/esp32_devkitc_v4-sch-20180607a.pdf)
- [WCH: документация CH340](https://www.wch-ic.com/downloads/CH340DS1_PDF.html)
- [Инструкция по проверке и доработке автозагрузки](CH340C_AUTO_BOOT_MOD.md)
