# Сравнение сжатия видео разными кодеками

Документ сводит в одну таблицу уникальные кодированные видео из каталогов
`out/BPV`, `out/HLV`, `out/H263`, `out/MJPEG`, `out/MPEG1`, `out/MPEG4SP` и
`out/DivX3`.

В таблицу не включены исходные и подготовленные видео из `out/sources`,
сопутствующие JSON/CSV-отчёты и копии тестовых роликов внутри проектов
прошивок. Размеры указаны в MiB (`1 MiB = 1 048 576 байт`).

## Общая сводка

| Семейство кодека | Количество файлов | Общий размер, MiB | Краткое описание |
| --- | ---: | ---: | --- |
| BPV v5/v6/v7 | 17 | 2 763,57 | Собственный блочно-палитровый формат BPV1; в отчётах доступен фактический RGB PSNR. |
| H.263 | 6 | 696,66 | Baseline H.263 в AVI, стандартный размер CIF 352x288. |
| MJPEG | 5 | 430,30 | Независимые JPEG-кадры в AVI; простое декодирование, но высокий объём. |
| HLV v14 | 5 | 342,82 | Собственный формат HLV с адаптивным выбором режимов блоков. |
| MPEG-1 | 5 | 176,06 | MPEG-1 Video в MPEG Program Stream без B-кадров. |
| MPEG-4 SP | 4 | 117,25 | MPEG-4 Part 2 Simple Profile в AVI, I/P-кадры. |
| DivX 3 | 5 | 115,19 | Microsoft MPEG-4 v3 (`msmpeg4v3`) в AVI, половина исходной частоты кадров. |
| **Итого** | **47** | **4 641,85** | Около 4,53 GiB уникальных кодированных видео. |

Суммарные размеры семейств нельзя напрямую трактовать как рейтинг сжатия:
число вариантов, длительность, разрешение, частота кадров и качество внутри
групп различаются. Для сравнения кодеков следует сопоставлять строки с одним
материалом, разрешением и близким качеством.

## Полная сравнительная таблица

| Файл | Материал | Параметры | Тип кодека / контейнер | Качество | Размер, MiB |
| --- | --- | ---: | --- | ---: | ---: |
| [BigBuckBunny_320x180_24fps_BPVv5_35dB.bpv1](../out/BPV/BigBuckBunny_320x180_24fps_BPVv5_35dB.bpv1) | Big Buck Bunny | 320x180, 24 FPS | BPV v5 / BPV1 | PSNR 35,08 dB; цель 40 dB | 210,68 |
| [BigBuckBunny_320x180_24fps_BPVv6_35dB.bpv1](../out/BPV/BigBuckBunny_320x180_24fps_BPVv6_35dB.bpv1) | Big Buck Bunny | 320x180, 24 FPS | BPV v6 / BPV1 | PSNR 34,59 dB; цель 40 dB | 212,27 |
| [BigBuckBunny_320x180_24fps_BPVv6_36dB.bpv1](../out/BPV/BigBuckBunny_320x180_24fps_BPVv6_36dB.bpv1) | Big Buck Bunny | 320x180, 24 FPS | BPV v6 / BPV1 | PSNR 36,11 dB; цель 40 dB | 204,72 |
| [BigBuckBunny_320x180_24fps_BPVv7_35dB.bpv1](../out/BPV/BigBuckBunny_320x180_24fps_BPVv7_35dB.bpv1) | Big Buck Bunny | 320x180, 24 FPS | BPV v7 / BPV1 | PSNR 35,11 dB; цель 40 dB | 156,45 |
| [BigBuckBunny_320x240_24fps_BPVv5_36dB.bpv1](../out/BPV/BigBuckBunny_320x240_24fps_BPVv5_36dB.bpv1) | Big Buck Bunny | 320x240, 24 FPS | BPV v5 / BPV1 | PSNR 35,52 dB; цель 40 dB | 286,47 |
| [BigBuckBunny_320x240_24fps_BPVv6_35dB.bpv1](../out/BPV/BigBuckBunny_320x240_24fps_BPVv6_35dB.bpv1) | Big Buck Bunny | 320x240, 24 FPS | BPV v6 / BPV1 | PSNR 34,94 dB; цель 40 dB | 288,24 |
| [BigBuckBunny_320x240_24fps_BPVv6_37dB.bpv1](../out/BPV/BigBuckBunny_320x240_24fps_BPVv6_37dB.bpv1) | Big Buck Bunny | 320x240, 24 FPS | BPV v6 / BPV1 | PSNR 36,56 dB; цель 40 dB | 278,30 |
| [BigBuckBunny_320x240_24fps_BPVv7_36dB.bpv1](../out/BPV/BigBuckBunny_320x240_24fps_BPVv7_36dB.bpv1) | Big Buck Bunny | 320x240, 24 FPS | BPV v7 / BPV1 | PSNR 35,50 dB; цель 40 dB | 213,77 |
| [Danila_320x180_30fps_BPVv5_35dB.bpv1](../out/BPV/Danila_320x180_30fps_BPVv5_35dB.bpv1) | Danila | 320x180, 30 FPS | BPV v5 / BPV1 | PSNR 34,72 dB; цель 40 dB | 100,47 |
| [Danila_320x180_30fps_BPVv6_34dB.bpv1](../out/BPV/Danila_320x180_30fps_BPVv6_34dB.bpv1) | Danila | 320x180, 30 FPS | BPV v6 / BPV1 | PSNR 34,18 dB; цель 40 dB | 100,50 |
| [Danila_320x180_30fps_BPVv6_35dB.bpv1](../out/BPV/Danila_320x180_30fps_BPVv6_35dB.bpv1) | Danila | 320x180, 30 FPS | BPV v6 / BPV1 | PSNR 35,45 dB; цель 40 dB | 99,29 |
| [Danila_320x180_30fps_BPVv7_35dB.bpv1](../out/BPV/Danila_320x180_30fps_BPVv7_35dB.bpv1) | Danila | 320x180, 30 FPS | BPV v7 / BPV1 | PSNR 34,52 dB; цель 40 dB | 92,25 |
| [Danila_320x240_30fps_BPVv5_35dB.bpv1](../out/BPV/Danila_320x240_30fps_BPVv5_35dB.bpv1) | Danila | 320x240, 30 FPS | BPV v5 / BPV1 | PSNR 34,81 dB; цель 40 dB | 132,34 |
| [Danila_320x240_30fps_BPVv6_34dB.bpv1](../out/BPV/Danila_320x240_30fps_BPVv6_34dB.bpv1) | Danila | 320x240, 30 FPS | BPV v6 / BPV1 | PSNR 34,11 dB; цель 40 dB | 132,66 |
| [Danila_320x240_30fps_BPVv6_35dB.bpv1](../out/BPV/Danila_320x240_30fps_BPVv6_35dB.bpv1) | Danila | 320x240, 30 FPS | BPV v6 / BPV1 | PSNR 35,42 dB; цель 40 dB | 131,25 |
| [Danila_320x240_30fps_BPVv7_34dB.bpv1](../out/BPV/Danila_320x240_30fps_BPVv7_34dB.bpv1) | Danila | 320x240, 30 FPS | BPV v7 / BPV1 | PSNR 34,49 dB; цель 40 dB | 122,88 |
| [VideoFormatRegression_320x240_30fps_BPVv6_35dB.bpv1](../out/BPV/VideoFormatRegression_320x240_30fps_BPVv6_35dB.bpv1) | Регрессионный тест | 320x240, 30 FPS | BPV v6 / BPV1 | PSNR 34,90 dB; цель 35 dB | 1,05 |
| [BigBuckBunny_320x180_24fps_HLVv14_42dB.hlv](../out/HLV/BigBuckBunny_320x180_24fps_HLVv14_42dB.hlv) | Big Buck Bunny | 320x180, 24 FPS | HLV v14 / HLV | профиль 42 dB | 92,75 |
| [BigBuckBunny_320x240_24fps_HLVv14_41dB.hlv](../out/HLV/BigBuckBunny_320x240_24fps_HLVv14_41dB.hlv) | Big Buck Bunny | 320x240, 24 FPS | HLV v14 / HLV | профиль 41 dB | 112,86 |
| [Danila_320x180_30fps_HLVv14_38dB.hlv](../out/HLV/Danila_320x180_30fps_HLVv14_38dB.hlv) | Danila | 320x180, 30 FPS | HLV v14 / HLV | профиль 38 dB | 62,54 |
| [Danila_320x240_30fps_HLVv14_38dB.hlv](../out/HLV/Danila_320x240_30fps_HLVv14_38dB.hlv) | Danila | 320x240, 30 FPS | HLV v14 / HLV | профиль 38 dB | 74,14 |
| [VideoFormatRegression_320x240_30fps_HLVv14_adaptive35-42dB.hlv](../out/HLV/VideoFormatRegression_320x240_30fps_HLVv14_adaptive35-42dB.hlv) | Регрессионный тест | 320x240, 30 FPS | HLV v14 / HLV | адаптивно 35–42 dB | 0,54 |
| [BigBuckBunny_352x288_24fps_H263_36dB.avi](../out/H263/BigBuckBunny_352x288_24fps_H263_36dB.avi) | Big Buck Bunny | 352x288, 24 FPS | Baseline H.263 / AVI | профиль 36 dB | 154,36 |
| [BigBuckBunny_352x288_24fps_H263_39dB.avi](../out/H263/BigBuckBunny_352x288_24fps_H263_39dB.avi) | Big Buck Bunny | 352x288, 24 FPS | Baseline H.263 / AVI | профиль 39 dB | 396,55 |
| [Danila_352x288_30fps_H263_29dB.avi](../out/H263/Danila_352x288_30fps_H263_29dB.avi) | Danila | 352x288, 30 FPS | Baseline H.263 / AVI | профиль 29 dB | 29,25 |
| [Danila_352x288_30fps_H263_36dB_q7.avi](../out/H263/Danila_352x288_30fps_H263_36dB_q7.avi) | Danila | 352x288, 30 FPS | Baseline H.263 / AVI | профиль 36 dB; Q7 | 54,60 |
| [Danila_352x288_30fps_H263_Q6.avi](../out/H263/Danila_352x288_30fps_H263_Q6.avi) | Danila | 352x288, 30 FPS | Baseline H.263 / AVI | Q6 | 61,26 |
| [VideoFormatRegression_352x288_30fps_H263_CIF_q6.avi](../out/H263/VideoFormatRegression_352x288_30fps_H263_CIF_q6.avi) | Регрессионный тест | 352x288, 30 FPS | Baseline H.263 CIF / AVI | Q6 | 0,64 |
| [BigBuckBunny_320x180_24fps_MJPEG_39dB.avi](../out/MJPEG/BigBuckBunny_320x180_24fps_MJPEG_39dB.avi) | Big Buck Bunny | 320x180, 24 FPS | Baseline MJPEG / AVI | профиль 39 dB | 130,77 |
| [BigBuckBunny_320x240_24fps_MJPEG_40dB.avi](../out/MJPEG/BigBuckBunny_320x240_24fps_MJPEG_40dB.avi) | Big Buck Bunny | 320x240, 24 FPS | Baseline MJPEG / AVI | профиль 40 dB | 148,86 |
| [Danila_320x180_30fps_MJPEG_40dB.avi](../out/MJPEG/Danila_320x180_30fps_MJPEG_40dB.avi) | Danila | 320x180, 30 FPS | Baseline MJPEG / AVI | профиль 40 dB | 67,34 |
| [Danila_320x240_30fps_MJPEG_40dB.avi](../out/MJPEG/Danila_320x240_30fps_MJPEG_40dB.avi) | Danila | 320x240, 30 FPS | Baseline MJPEG / AVI | профиль 40 dB | 82,33 |
| [VideoFormatRegression_320x240_30fps_MJPEG_q3.avi](../out/MJPEG/VideoFormatRegression_320x240_30fps_MJPEG_q3.avi) | Регрессионный тест | 320x240, 30 FPS | Baseline MJPEG / AVI | Q3 | 1,00 |
| [BigBuckBunny_320x180_24fps_MPEG1_41dB.mpg](../out/MPEG1/BigBuckBunny_320x180_24fps_MPEG1_41dB.mpg) | Big Buck Bunny | 320x180, 24 FPS | MPEG-1 Video / MPEG-PS | профиль 41 dB | 39,18 |
| [BigBuckBunny_320x240_24fps_MPEG1_40dB.mpg](../out/MPEG1/BigBuckBunny_320x240_24fps_MPEG1_40dB.mpg) | Big Buck Bunny | 320x240, 24 FPS | MPEG-1 Video / MPEG-PS | профиль 40 dB | 43,22 |
| [Danila_320x180_30fps_MPEG1_41dB.mpg](../out/MPEG1/Danila_320x180_30fps_MPEG1_41dB.mpg) | Danila | 320x180, 30 FPS | MPEG-1 Video / MPEG-PS | профиль 41 dB | 41,74 |
| [Danila_320x240_30fps_MPEG1_41dB.mpg](../out/MPEG1/Danila_320x240_30fps_MPEG1_41dB.mpg) | Danila | 320x240, 30 FPS | MPEG-1 Video / MPEG-PS | профиль 41 dB | 51,16 |
| [VideoFormatRegression_320x240_30fps_MPEG1_q3.mpg](../out/MPEG1/VideoFormatRegression_320x240_30fps_MPEG1_q3.mpg) | Регрессионный тест | 320x240, 30 FPS | MPEG-1 Video / MPEG-PS | Q3 | 0,77 |
| [BigBuckBunny_320x240_24fps_MPEG4SP_35dB.avi](../out/MPEG4SP/BigBuckBunny_320x240_24fps_MPEG4SP_35dB.avi) | Big Buck Bunny | 320x240, 24 FPS | MPEG-4 Part 2 Simple Profile / AVI | профиль 35 dB; Q5 | 44,03 |
| [Danila_320x240_30fps_MPEG4SP_35dB.avi](../out/MPEG4SP/Danila_320x240_30fps_MPEG4SP_35dB.avi) | Danila | 320x240, 30 FPS | MPEG-4 Part 2 Simple Profile / AVI | профиль 35 dB; Q5 | 31,28 |
| [Danila_320x240_30fps_MPEG4SP_SPEED_q7.avi](../out/MPEG4SP/Danila_320x240_30fps_MPEG4SP_SPEED_q7.avi) | Danila | 320x240, 30 FPS | MPEG-4 Part 2 SP, speed / AVI | Q7; профиль скорости | 41,47 |
| [VideoFormatRegression_320x240_30fps_MPEG4SP_35dB.avi](../out/MPEG4SP/VideoFormatRegression_320x240_30fps_MPEG4SP_35dB.avi) | Регрессионный тест | 320x240, 30 FPS | MPEG-4 Part 2 Simple Profile / AVI | профиль 35 dB; Q5 | 0,47 |
| [BigBuckBunny_320x180_12fps_DivX3_40dB.avi](../out/DivX3/BigBuckBunny_320x180_12fps_DivX3_40dB.avi) | Big Buck Bunny | 320x180, 12 FPS | DivX 3 / `msmpeg4v3` / AVI | профиль 40 dB | 29,25 |
| [BigBuckBunny_320x240_12fps_DivX3_41dB.avi](../out/DivX3/BigBuckBunny_320x240_12fps_DivX3_41dB.avi) | Big Buck Bunny | 320x240, 12 FPS | DivX 3 / `msmpeg4v3` / AVI | профиль 41 dB | 34,12 |
| [Danila_320x180_15fps_DivX3_40dB.avi](../out/DivX3/Danila_320x180_15fps_DivX3_40dB.avi) | Danila | 320x180, 15 FPS | DivX 3 / `msmpeg4v3` / AVI | профиль 40 dB | 23,12 |
| [Danila_320x240_15fps_DivX3_40dB.avi](../out/DivX3/Danila_320x240_15fps_DivX3_40dB.avi) | Danila | 320x240, 15 FPS | DivX 3 / `msmpeg4v3` / AVI | профиль 40 dB | 28,43 |
| [VideoFormatRegression_320x240_15fps_DivX3_q3.avi](../out/DivX3/VideoFormatRegression_320x240_15fps_DivX3_q3.avi) | Регрессионный тест | 320x240, 15 FPS | DivX 3 / `msmpeg4v3` / AVI | Q3 | 0,29 |

## Интерпретация качества

- Для BPV указан фактически измеренный RGB PSNR из соответствующего
  JSON-отчёта. Также приведена цель поиска качества.
- Для остальных кодеков значения в dB — маркировка профиля выходного файла.
  Это не обязательно повторно измеренный фактический PSNR.
- `Q3`, `Q5`, `Q6` и `Q7` — параметры квантования конкретных кодеков. Они не
  образуют общую шкалу и не должны напрямую сравниваться между кодеками.
- DivX 3 использует половину исходной частоты кадров: 12 FPS для Big Buck Bunny
  и 15 FPS для Danila. Это заметно влияет на размер и делает прямое сравнение с
  полночастотными вариантами условным.
