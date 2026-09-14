# GTACHECK — каталог правил, раунд 3 (машины, оружие, педы, одежда, катсцены, GXT/SCM/IMG/UI, аудио)

Источник: `E:\RE\addon_check\vehicle_rules.md`, `weapon_ped_rules.md`, `clothes_cutscene_rules.md`,
`text_script_img_rules.md`, `audio_rules.md` (gta_sa.exe 1.0 US, декомпиляция Ghidra + capstone) и
**вердикты адверсарной верификации**, которые здесь уже применены: опровергнутые правила выброшены или
понижены, уточнённые помечены «(уточнено верификацией)». Все правила калиброваны на чистой SA 1.0 US:
ваниль даёт 0 Краш / 0 Ошибка (кроме отмеченных Инфо/Предупреждение).

Уровни как в gtacheck: **Краш** (падение/зависание/LOAD-FAIL), **Ошибка** (видимый мусор/порча памяти),
**Предупреждение** (подозрительно, молча игнорируется), **Инфо**.

Условные обозначения: `LoadLine` = CFileLoader::LoadLine 0x536F80 (байты <0x20 и «,» → пробел, буфер 512);
`ReadLine` = CFileMgr::ReadLine 0x5389B0 = голый fgets; «FILE* не проверен» = OpenFile → LoadLine/ReadLine
без NULL-теста → fgets(NULL) → _lock_str 0x823FA2 EnterCriticalSection(NULL+0x20).

---

## 0. Уже реализовано в gtacheck (check_veh.cpp, собрано и откалибровано)

| gtacheck | Уровень | Что | Из отчёта |
|---|---|---|---|
| VEH-01 | Краш | car/mtruck/quad/heli/plane/trailer без wheel_lf/rf/lb/rb_dummy → GetWheelPosn 0x4C7D20 (0x4C7DAD / 0x4C7D57) без NULL-теста | VEH-01 |
| VEH-02 | Краш | bike/bmx без wheel_front / wheel_rear → CBike::SetupSuspensionLines 0x6B8AC9 rep movsd | VEH-02 |
| VEH-03 | Ошибка | нет chassis_dummy (car/mtruck/quad/heli/trailer/bike/bmx/f_heli) | VEH-22 (часть) |
| VEH-04 | Предупр. / Инфо | дубликаты имён фреймов из таблицы типа / вне таблицы (SetFrameIds 0x4C5460 метит только первый) | VEH-17 |
| VEH-05 | Предупр. | имя из таблицы с блендеровским суффиксом .NNN | — |
| VEH-06 | Инфо | покрытие таблицы ms_vehicleDescs по машине | — |
| VEH-07 | Ошибка | Collision Plugin 0x0253F2FA отсутствует или тело не начинается с COL3 | VEH-05 + VEH-06 |
| VEH-08 | пропущено | >50 моделей машин одновременно (пул VehicleStruct) — не решается оффлайн | VEH-08 |
| VEH-09 | Инфо | поезда (тип 6) — своя схема колёс, VEH-01/02 не применяются | — |

Рекомендуемые правки к уже сделанному:
* **VEH-07**: разделить. Плагин отсутствует **и** нет `.col`-записи с именем модели → **Краш** (VEH-05 подтверждён:
  CEntity::GetColModel 0x535300 возвращает modelinfo+0x14 без проверки, CAutomobile::SetupSuspensionLines 0x6A65EF
  `mov esi,[eax+0x2c]`, CBike 0x6B89C3). Плагин есть, но fourcc не COLL/COL2/COL3 → **Предупреждение**
  (ванильный rccam.dff: тело `f2 d7 a8 3e`, 160 байт).
* **DAT-11** (cars: <15 полей): текст «хвостовые поля останутся по умолчанию» неверен — для типа plane
  LoadVehicleObject всё равно пишет wheelScale из **неинициализированных** локалов (0x5B7267/0x5B726A);
  только id и upgradeClass предустановлены в −1. Ванильный skimmer имеет 11 полей — уровень оставить
  Предупреждение, текст: «масштаб колёс = мусор со стека».

---

## 1. Лимиты (сводно, с поправками верификации)

| Область | Лимит | Адрес / источник |
|---|---|---|
| handling.cfg | **210 фиксированных имён** (162 car 0..161, 13 bike 162..174, 12 boat 175..186, 24 flying 186..209); своих имён быть не может | таблица 0x8D3978, ячейки 14 байт; FindExactWord 0x6F4F30 (prefix strncmp!) |
| handling.cfg | tHandlingData 0xE0×210 @gHandlingDataMgr+0x14; bike 0x40×13 @+0xB7D4; flying 0x58×24 @+0xBB14; boat 0x3C×12 @+0xC354; numGears 1..5 (m_aGears[6]); anim groups 30 (0xC1CDC0, 0x94 каждая); токенов: car 36, `!` 17, `$` 23, `%` 16, `^` 36; **`$` = flying, `%` = boat** | LoadHandlingData 0x5BD830 |
| carcols.dat | 128 цветов (0xB4E480..0xB4E680), индексы — байты; ≤8 вариаций car (16 чисел) / car4 (32); буфер строки 1024 | LoadVehicleColours 0x5B6890 |
| carmods.dat | 18 слотов апгрейдов (16 + hydralics + stereo); 30 линков (0xB4E6D8); 4 группы колёс × 15 (0xB4E3F8, счётчики 0xB4E470) | LoadVehicleUpgrades 0x5B65A0 |
| cargrp.dat | 34 группы × 23 модели (0xC0ED38, счётчики 0xC0EC78); буфер 1024 | LoadCarGroups 0x5BD1A0 |
| vehicle DFF | пул CVehicleStructure 50 (0xB4E680); 6 extras; 18 позиций апгрейдов; 15 dummy; 4 remap-TXD (+0x2FA); тело collision-плагина ≤ 16384 (PC_Scratch 0xC8E0C8 — размер из gta-reversed, в exe только «следующий символ gamma @0xC92134») | SetClump 0x4C95C0, ClumpCollisionStreamRead 0x41B1D0 |
| vehicles.ide | буферы: model 24, txd 24, type 8, handling 16, gameName 32, anims 16, class 16; wheelUpgradeClass −1..3 | LoadVehicleObject 0x5B6F30 |
| тип IDE → таблица фреймов | car 0, mtruck 1, quad 2, heli & f_heli 3, plane 4, boat 5, train 6, f_plane 8, bike 9, bmx 10, trailer 11 (ms_vehicleDescs 0x8A7740); ms_wheelFrameIDs {5,7,2,4} = wheel_lf/lb/rf/rb_dummy | |
| weapon.dat | aWeaponInfo 80 (типы 0..48, skill-строки type+25/+36/+47 для 22..32) 0xC8AAB8..; aGunAimingOffsets 21 (0xC8A8A8); слоты 0..12 (m_aWeapons[13]); `$` 26(+4) полей, `£` 12, `%` 10; 49 имён (0x8D6150, без регистра); 6 fireType (с регистром); 8 baseCombo | CWeaponInfo::Initialise 0x5BF750 |
| animgrp | 118 встроенных групп (0x8AA5A8) + ≤27 из файла = 145; имена группы/блока ≤15, анимации ≤23 | |
| ped.dat | 32 имени типов (0x8D23B8, с регистром); массив acquaintance new(0x284) ровно | CPedType::Initialise 0x608E40 |
| pedstats.dat | 43 строки (new(0x8BC)), имя ≤23, 11 полей, defaultDecisionMaker 0..7 (**читается как signed char**) | CPedStats::Initialise 0x5BF9D0 |
| pedgrp.dat | ровно 57 групп × ≤21 модель (0xC0F358 / 0xC0ECC0); токен <256, строка <1024 | LoadPedGroups 0x5BCFE0 |
| popcycle.dat | ровно 480 строк данных (20 зон × 2 × 12) × 24 int 0..255; сумма 18 групп > 0 | CPopCycle::Initialise 0x5BC090 |
| PedEvent.txt / allowed/*.ped | ≤41 событие, id 0..95 (m_EventIndices[96] @+0xC034); .ped-строка = 80 чисел через запятую, ≤255 символов (ReadLine 0x100!) | 0x5BB9F0 / 0x6076B0 |
| decision makers | 20 слотов (8 ped + 2 group + 10 mission), 41 решение (CDecisionMaker 0x99C) | |
| surfaces | 179 имён (0..178, с регистром); surface.dat 6×6; surfinfo 36 полей; surfaud 10 | GetSurfaceIdFromName 0x55D220 |
| shopping.dat | ≤560 items в prices; ≤300 на подсекцию; ≤300 на shop; nametag ≤7; 11 имён price-секций | CShopping 0x49B6A0.. |
| statdisp / ar_stats | ≤128 строк (0xB78200); gxt-метка ≤7; statId **0..81 или 120..343**; ar_stats id 0..59 | 0x559860 / 0x5599B0 |
| player.img | CDirectory 550 записей (ваниль 542); стриминговый буфер = наибольшая запись стрим-IMG, округлённая до чётного числа секторов (ваниль 1264) | CDirectory::Init 0x5A4190 |
| одежда | ms_clothesRules 600 слов (ваниль 587; реальная порча с 607 = PlayerClothes 0xBC1C78); ≤8 активных IGNORE/EXCLUSIVE; 10 частей; сумма вершин normal-клампов ≤65535 | |
| cuts.img / .cut | 512 записей (ваниль 444); model ≤50 слотов, text ≤64, uncompress ≤8 (безопасно 7), attach ≤50, remove ≤50, peffect ≤8; ≤20 special-моделей; имя катсцены ≤7 (порча с 12) | CCutsceneMgr 0x4D5A20 |
| GXT | TABL ≤200 (0x960 @CText+0x12C), имена ≤7; TKEY ≤32767 на таблицу; CText::Get статический буфер 25; неизвестный чанк должен быть < 0x10000 (16-битный счётчик пропуска) | CText::Load 0x6A01A0 |
| SCM | ScriptSpace 200000 + миссия 69000 (0xA49960 / 0xA7A6A0); объектов ≤395; миссий ≤200; локалов миссии ≤1024; стрим-скриптов ≤82 (имена 20 байт, id 26230..26311); текстовые метки 8 байт | |
| IMG | 8 слотов (0x8E48D8 ×0x30; 2 встроенных + строки gta.dat + player.img → ≤5 строк IMG); имя 24 байта (точка на индексе ≤20); **ms_aInfoForModel 26316** (DFF 20000, TXD 5000, COL 255, IPL 256, DAT 64, IFP 180, RRR 475, SCM 82); ms_pExtraObjectsDir 550; **буфер стриминга = наибольшая запись (не ×2)**, делится на 2 канала; memory 0x3200000 и vehicles 22 форсируются после stream.ini | CStreaming::Init2 0x5B9020 |
| UI | gFontData 2 шрифта (FONT_ID 0/1), PROP 26×8; радар 144 тайла; HUD 6 спрайтов + 64 блипа; loadscs 15 + nvidia/eax/title_pc_US | |
| аудио | BankSlot: N×0x12D4 (exe просит слоты 0..42, кроме 38/39; 43/44 не просит); BankLkup N×12 {u8 pak, u32 off, u32 size}, exe ссылается на банки 0..709; PakFiles N×52; заголовок банка 0x12C4 {i16 numSounds; 400×{u32 off,u32 loop,u16 rate,i16 headroom}}; StrmPaks N×16 (сток 17 записей, №2 пустая); TrakLkup N×12, треков 1922; заголовок трека 0x1F84 до OggS; XOR-ключ потоков **EA 3A C4 A1 9A A8 14 F3 48 B0 D7 23 9D E8 FF F1** (byte[i] ^= key[(pos+i)&15], 0x4F17D0); vehicles audio table 0x860AF0 — 231 строка (400..630) | CAEBankLoader 0x4DFB10.., CAEMP3TrackLoader 0x4E0930.. |

---

## 2. Машины — vehicle DFF (новые правила, нумерация продолжает VEH-09)

| id | Уровень | Условие | Как проверить оффлайн | Движок |
|---|---|---|---|---|
| VEH-10 | Краш | фрейм `extra1..extra6` не владеет атомиком (отчёт VEH-03) | индекс каждого extraN-фрейма должен быть frame-индексом какого-то ATOMIC-чанка | PreprocessHierarchy 0x4C8E60 (0x4C8FF9/0x4C9007) → RpClumpRemoveAtomic 0x74A4C0: `mov ecx,[eax+0x44]` без NULL-теста |
| VEH-11 | Краш | DUMMY-фрейм таблицы (ped_frontseat, ped_backseat, headlights(2), taillights(2), exhaust, engine, petrolcap, hookup, ped_arm, miscpos_a..d; plane: aileron_pos, elevator_pos, rudder_pos, wingtip_pos; bike/bmx: bargrip; train: ped_left/mid/right_entry; f_plane: light_tailplane/left/right) владеет атомиком (отчёт VEH-04) | ни один ATOMIC не ссылается на dummy-фрейм | позиция копируется (0x4C8F24), затем RwFrameDestroy 0x4C8F6E → RwFreeListFree; атомик держит указатель на освобождённый фрейм = **use-after-free** (уточнено: сначала может проявиться как смещённая деталь, потом краш) |
| VEH-11b | Предупр. | у DUMMY-фрейма есть дочерние фреймы | parent любого фрейма не должен быть dummy-именем | RW 3.7 _rwFrameInternalDeInit: детям ставится parent=NULL (осиротели, не удалены) |
| VEH-12 | Краш | тело Collision Plugin > 16384 байт (отчёт VEH-07, уточнено верификацией) | длина extension-чанка 0x0253F2FA | RwStreamRead(stream, PC_Scratch 0xC8E0C8, chunkLength) 0x41B1FB без ограничения |
| VEH-12b | Ошибка | fourcc валиден (COLL/COL2/COL3), но поле size ≠ payload+24 или тело < 0x20 | **только при валидном fourcc** (rccam.dff иначе ловится ложно) | LoadCollisionModelVer2/3 читают size−0x18 из scratch → устаревшие байты 16-КБ буфера, не доказанный краш |
| VEH-13 | Ошибка | имя handling в vehicles.ide не одно из 210 (точно, верхний регистр, ≤14) (отчёт VEH-09, уточнено) | сравнение с таблицей §1.4 отчёта | GetHandlingId 0x6F4FD0 → 210; &m_aVehicleHandling[210] = **gHandlingDataMgr+0xB7D4 = m_aBikeHandling[0] (BIKE)**, читается как tHandlingData: mass=0.35 (leanFwdCOM), turnMass=0.34 |
| VEH-14 | Предупр. | тип bike/bmx с индексом handling не в 162..174; boat не в 175..186; heli/plane/f_heli/f_plane не в 186..209 (отчёт VEH-10) | индекс имени в таблице vs тип IDE | CBike ctor 0x6BF51B: 0xC3491C+id*0x40 без проверки; GetBoatPointer 0x6F5300 → PREDATOR; GetFlyingPointer 0x6F52D0 → SEAPLANE |
| VEH-15 | Краш | compRules: rule-ниббл 1/2/3 при всех трёх comp-нибблах F, или любой comp-ниббл 6..14 — **когда в DFF есть ≥1 extra** (отчёт VEH-11) | hex: A comps биты 0-11, A rule 12-15, B comps 16-27, B rule 28-31 | ChooseComponent 0x4C7FB0: CountCompsInRule=0 → GetRandomNumberInRange(0,0)=0 → возвращает ниббл 0 = 0xF; CreateInstance 0x4C96E2 `[edx+ebx*4+0x2F4]` с ebx=15 → смещение 0x330 за CVehicleStructure (0x314) → RpAtomicClone(мусор) |
| VEH-15b | Предупр. | comp-ниббл < 6, но ≥ числа extras в DFF | сравнить с количеством extraN-фреймов | m_apExtras[n] = NULL → компонент молча пропускается |
| VEH-16 | Предупр. | anims ≠ `null` (**_stricmp**, т.е. NULL/Null тоже null) и нет `<anims>.ifp` ни в одном IMG (без регистра) (отчёт VEH-12, уточнено) | сравнить с .ifp в каталогах IMG | SetAnimFile 0x4C7670 → GetAnimationBlockIndex 0x4D3990 (_stricmp 0x8229B6) → −1 → блок не стримится с машиной |
| VEH-17 | Ошибка | car-семейство: wheelScaleFront/Rear ≤ 0 **если поле присутствует**; bike/bmx: угол руля (поле 12) ≤0 или ≥90 (отчёт VEH-14, уточнено) | числовой диапазон только по присутствующим полям; отсутствующие → Предупреждение «мусор со стека» (см. DAT-11) | 0x5B7267/0x5B726A (+0x40/+0x44); bikes 0x5B720A (+0x58 = (float)wheelModelId); CBike ctor tan(deg2rad) |
| VEH-18 | Ошибка | wheelUpgradeClass не в {−1,0,1,2,3} (отчёт VEH-15) | диапазон | GetNumWheelUpgrades 0x4C8740 `movsx word [0xB4E470+class*2]`, GetWheelUpgrade 0x4C8750 без границ |
| VEH-19 | Краш / Ошибка | `models/generic/vehicle.txd` без platecharset/plateback1/2/3 → Краш; без vehiclelights128/vehiclelightson128 → Ошибка; `particle.txd` без `white` → Краш (отчёт VEH-19) | списки имён текстур | CCustomCarPlateMgr::Initialise 0x6FD500: RwTextureRead → `mov byte [eax+0x50],1` без NULL (0x6FD525/3F/57/6F); LoadEnvironmentMaps 0x4C8780 пишет ms_pWhiteTexture+0x10; SetupCommonData 0x5B8F00 |
| VEH-20 | Инфо | >4 TXD `<модель><цифры>.txd` для одной машины (400..630) → 5-й и далее уходят в паддинг +0x302 (отчёт VEH-20) | сканирование каталогов IMG | AssignRemapTxd 0x4C9360 (срезает **все** хвостовые цифры) → AddRemap 0x4C86D0, цикл `i<4` |
| VEH-20b | Инфо | имя мод-модели кончается цифрами и после их среза совпадает с другой машиной (`car2` при существующем `car`) — её TXD зарегистрируется как remap чужой машины | срез цифр + поиск среди cars | то же |
| VEH-21 | Предупр. | отсутствуют универсальные dummy типа (отчёт VEH-22, **список уточнён верификацией по 212 DFF**): car — chassis chassis_dummy headlights ped_frontseat + 4 колеса; bike — chassis chassis_dummy chassis_vlo engine exhaust forks_front forks_rear handlebars headlights mudguard ped_frontseat taillights wheel_front wheel_rear; bmx — bargrip chainset chassis chassis_dummy chassis_vlo forks_front handlebars headlights ped_frontseat pedal_l pedal_r taillights wheel_front wheel_rear (**без** engine/exhaust); plane — chassis ped_frontseat rudder + 4 колеса (**без** chassis_dummy); boat — только ped_frontseat; train — bogie_front bogie_rear chassis chassis_dummy chassis_vlo headlights taillights wheel + 8 wheel_*1/2_dummy; heli — chassis chassis_dummy chassis_vlo moving_rotor static_rotor ped_frontseat + 4 колеса; trailer — chassis chassis_dummy chassis_vlo hookup + 4 колеса | наличие имён по типу | PreprocessHierarchy: m_avDummyPos остаётся (0,0,0) → педы/эффекты в начале координат |

## 3. Машины — handling.cfg / carcols.dat / carmods.dat / cargrp.dat (HND-*)

| id | Уровень | Условие | Как проверить | Движок |
|---|---|---|---|---|
| HND-01 | Краш | нет data/handling.cfg, carcols.dat, carmods.dat или cargrp.dat | существование (modloader) | FILE* не проверен: 0x5BD855, 0x5B68B0, 0x5B65C0, 0x5BD1C0 |
| HND-02 | Краш | handling.cfg: пустая или пробельная строка (после LoadLine-нормализации пусто и не `;`), строка данных без токена, строка из одного `!` | нормализовать как LoadLine (ctrl и `,` → пробел, trim); пусто и не начинается с `;` | car-ветка 0x5BE1AC strtok → 0x5BE1B8 `jmp 0x5BE1C0` → case 0 → FindExactWord(NULL) → strncmp(NULL); `!`-ветка → ConvertBikeDataToGameUnits(предыдущий указатель = NULL для первой). Ваниль: 0 пустых строк до `;the end` |
| HND-03 | Ошибка | строка данных (любой префикс, включая `#`-комментарии и строчные имена), имя которой не из 210 | таблица; регистр важен | FindExactWord 0x6F4F30: `strncmp(token, cell, strlen(cell))` → 210; car-строка пишет 0xE0 байт в +0xB7D4 = m_aBikeHandling[0..3] (BIKE, MOPED, DIRTBIKE, начало FCR900); `!` → +0xC3D4 (boat[2] REEFER+8 → RIO); `$` → +0xC354 (PREDATOR + 0x1C SPEEDER); `%` → boat[0] |
| HND-03b | Предупр. | имя совпадает только по префиксу (BUSX → BUS) | prefix-match | тот же strncmp по длине ячейки |
| HND-04 | Ошибка | `!` с id не в 162..174; `$` (flying) не в 186..209; `%` (boat) не в 175..186 | индекс по префиксу | база `!` 0x8F54+id*0x40 (0x5BD915), `$` 0x7B24+id*0x58 (0x5BDA5D): `$ LANDSTAL` пишет 0x58 байт в m_aVehicleHandling[140] EMPEROR+0x90; `%` → GetBoatPointer fallback |
| HND-05 | Предупр. | токенов ≠ car 36 / `!` 17 / `$` 23 / `%` 16 / `^` 36; нечисловое числовое поле; driveType не F/R/4; engineType не P/D/E | счёт токенов после нормализации | fixed-slot switch 0x5BE1C0..; недостающие поля = старые/нулевые значения; первый символ токена хранится как есть (+0x74/+0x75) |
| HND-06 | Ошибка | car: mass ≤ 0, percentSubmerged ≤ 0 или > 255, numGears = 0 или ≥ 6, maxVel ≤ 0, steeringLock ≤ 0 | диапазоны | ConvertDataToGameUnits 0x6F5080: 1/mass (0x6F50C7), mass*0.8/percentSubmerged (0x6F50D6); InitGearRatios 0x6D0460: numGears 0 → все передачи 0 (машина не едет); ≥6 → передача 6 пишется в +0x74..+0x7C (driveType/engineType/numGears/flags/engineAccel) и цикл перечитывает испорченный numGears |
| HND-06b | Инфо | frontLights/rearLights не в 0..3 (уточнено верификацией: читатель +0xDC/+0xDD не найден) | диапазон | только байтовые записи 0x5BE40B/0x5BE419 |
| HND-07 | Ошибка | `^`-строка: первое поле не в 0..29 или ≠ 36 токенов; car-строка поле 35 (animGroup) ≥ 30 (отчёт VEH-13) | диапазон | 0x5BE182 `imul ecx,0x94; add ecx,0xC1CDC0` без границ (30 — из gta-reversed NUM_VEH_ANIM_GROUPS); поле 35 → байт +0xDE (0x5BE427) |
| HND-08 | Краш / Ошибка | carcols car/car4: имя не является моделью → Краш; модель есть, но не cars → Ошибка | имя в cars | 0x5B6B1B GetModelInfo → 0x5B6B2F `mov [esi+0x2d0],al` с esi=NULL; для не-машины запись +0x2B0..+0x2D0 за пределами меньшего modelinfo |
| HND-09 | Ошибка | col-секция > 128 записей | счёт | ms_vehicleColourTable 0xB4E480 = 128×4, дальше ms_pVehicleStructurePool 0xB4E680 |
| HND-09b | Предупр. | индекс цвета car/car4 ≥ числа col-записей (ваниль: moonbeam 227 при 127 → **обязательно Предупреждение**); RGB вне 0..255 | сравнить | ChooseVehicleColour 0x4C8500, индексы — байты |
| HND-09c | Инфо | col-строка с < 3 целых (ваниль №98 `77.93,96` — sscanf останавливается на `.93`, g/b берутся со стека предыдущей строки) | парсинг `%d %d %d` | 0x5B6A22..0x5B6A7C |
| HND-10 | Инфо | машина отсутствует в carcols (цвета 0); > 16 чисел на car / > 32 на car4 (ваниль stafford 18 — Инфо); col без `#` | кросс-ссылка / счёт | форматы 0x8696E4 / 0x869680; variations = (n−1)/2 или /4 |
| HND-11 | Краш / Ошибка | carmods: любое имя модели (машина в mods, части, пары link, списки wheel, плюс встроенные hydralics/stereo) не определено ни в одном IDE → Краш; машина mods не cars → Ошибка | имя в любом IDE; ваниль: veh_mods.ide определяет 1086 stereo / 1087 hydralics | SetupVehicleUpgradeFlags 0x4C4570 (трамплин → 0x4C4576 `mov ax,[ecx+0x12]`) с ECX=GetModelInfo()=NULL в 0x5B66F1/0x5B6775/0x5B67B8/0x5B67E3/0x5B6827/0x5B684A; EBP машины не проверен 0x5B6742, `mov [ebx],cx` 0x5B6784 |
| HND-12 | Ошибка | > 16 частей в mods-строке; > 30 link-строк; группа wheel не в 0..3; > 15 моделей в группе | счёт (ваниль: 23 линка, ≤16 частей, группы 0..2) | m_anUpgrades[18]: 17-я часть затирает m_anRemapTxds +0x2FA и m_nAnimBlockIndex +0x304 (краш стриминга); AddUpgradeLink 0x4C74B0 (31-й затирает счётчик +0x78); AddWheelUpgrade 0x4C8700 |
| HND-13 | Предупр. | имя части без известного префикса (уточнено верификацией — полный список: `chss_ wheel_ exh_ fbmp_ rbmp_ misc_a_ misc_b_ misc_c_` (таблица 2, 0x4C46BB..) и `bnt_ bntl_ bntr_ spl_ wg_l_ wg_r_ fbb_ bbb_ lgt_ rf_ nto_ hydralics stereo` (таблица 1, 0x4C4596..)); всё по strncmp(prefix) — `hydralicsX` тоже матчится | prefix-тест; ваниль remingtn использует misc_c_lr_rem1/2/3 | SetupVehicleUpgradeFlags: без совпадения — слот не назначен; ранний выход, если бит 15 в [ecx+0x12] уже стоит |
| HND-14 | Предупр. | cargrp.dat: последняя строка без `\n`; строка ≥ 1024 байт (**понижено верификацией**: скан ищет 0x0A мимо NUL, но обычно упирается в старый 0x0A в буфере; за буфер выходит только если последняя строка самая длинная в файле) | последний байт; длины строк (ваниль: CRLF, макс 287) | LoadCarGroups 0x5BD1F0..; ReadLine(0x400) режет длинные строки |
| HND-15 | Краш / Предупр. | имя — модель, но не cars → Краш; имя не модель → Предупреждение (молча пропущено) | имя в cars / в IDE | id хранится без проверки типа → CCarCtrl строит машину из чужого modelinfo; NULL пропускается |
| HND-16 | Ошибка | > 34 групп (строк с ≥1 принятым именем) | счёт | m_nNumCarsInGroup[34] 0xC0EC78 (+4 байта паддинга: 35/36-я в паддинг, 37-я → m_nNumPedsInGroup); m_aCarGroups кончается 0xC0F354, 35-я группа с 3-й модели (и 2000-заливка) пишет в m_PedGroups[0] 0xC0F358 → педы со id машин |
| HND-16b | Инфо | > 23 имён в строке (ваниль до 29) | счёт | цикл `< 0x17`, остальное игнорируется |
| HND-17 | Инфо | handling.cfg без `;the end`; строка > 511 | скан | 0x5BD890 `repe cmpsb` 9 байт; LoadLine 0x200 |

Опровергнуто/выброшено: VEH-09 арифметика (исправлена в VEH-13), VEH-12 «только строчное null», VEH-14 «отсутствующее поле = 0», VEH-22 общий chassis (заменён VEH-21), HND-13 неполный список, HND-14 Краш.

---

## 4. Оружие — weapon.dat (WPN-*)

| id | Уровень | Условие | Как проверить | Движок |
|---|---|---|---|---|
| WPN-01 | Краш | файл отсутствует | существование | LoadWeaponData 0x5BE670 (OpenFile 0x5BE68A → LoadLine 0x5BE699) |
| WPN-02 | Ошибка | `$` < 26 полей, `£` (0xA3) < 12, `%` < 10; числовое поле не парсится | токенизация как LoadLine (файл открыт `rb`), счёт, проверка %d/%f/%x | sscanf 0x5BE6EE / 0x5BE95B / 0x5BECBD; предобнулены только байт 0 четырёх %s-буферов и 4 хвостовых float `$` (0x5BE92F..); остальное — мусор со стека |
| WPN-03 | Ошибка / Краш (уточнено верификацией) | любой %s-токен ≥ 32 → Ошибка; **Краш**: `£` 9-й токен (baseCombo) ≥ 32; name ≥ 96; fireType ≥ 64; animGroup ≥ 128; префиксный токен ≥ 136 | длины токенов | фрейм 0x5BE670: prefix esp+0x98[8], animGroup +0xA0[32], name +0xC0[32], fireType +0xE0[32], baseCombo +0x100[32], локалы до +0x120, далее EBP/ret; 32-символьная группа пишет NUL в name[0] → FindWeaponType('') = тип 47 |
| WPN-04 | Ошибка | имя не из 49 (без регистра; индекс 47 = пустая строка) | список §1.1 отчёта | FindWeaponType 0x743D10 (_stricmp 0x8229B6) → 0 → строка пишется поверх UNARMED |
| WPN-05 | Ошибка (уточнено: не «игнорируется») | fireType не ровно MELEE/INSTANT_HIT/PROJECTILE/AREA_EFFECT/CAMERA/USE | точное сравнение с регистром | FindWeaponFireType 0x5BCF30 → `bVar4*4+1` = INSTANT_HIT для любой неизвестной строки: оружие ближнего боя стреляет пулями |
| WPN-06 | Ошибка | `$` для типов 22..32 (PISTOL..TEC9) со skill не в 0..3 | int 0..3 | 0x5BE968..0x5BE9B0: индекс = local_110 предыдущей строки (на первой — неинициализирован) → переписывается чужая строка |
| WPN-07 | Краш | modelId1 или modelId2 ≠ −1 не определён ни в одном IDE (или ≥ 20000) | id в IDE | загрузка: `if (0 < modelId1)` → ms_modelInfoPtrs[id]+0x24 (0x5BEC3E..49) без NULL — для `£` и `$` со skill 1 (кроме 40 DETONATOR); прочие — в рантайме RequestModel 0x4087E0 (0x40890A, +0xA); ≥20000 читает за 0xA9B0C8+0x13880 |
| WPN-08 | Краш / Ошибка (уточнено) | modelId1 определён не weap-строкой: **tobj / damage (флаг 0x1000) / LOD-атомик** (размер 0x24) → запись +0x24 затирает vtable следующего modelinfo → Краш; objs (0x20) → m_nKey соседа, peds → anim group, cars → clump-info → Ошибка | секция-определитель | 0x5BEC49 пишет +0x24 (CWeaponModelInfo::m_weaponInfo) |
| WPN-09 | Ошибка | slot не в 0..12 | int | CPed::GiveWeapon 0x5E6080: `ecx=[eax+0x14]; imul 0x1C; +0x5A0` без границ; slot берётся из строки skill 1 |
| WPN-10 | Ошибка | `$` animGroup / `£` stealthAnimGroup не `null` (strncmp 4 — любой токен с `null`) и не точное имя из 118 встроенных / animgrp.dat (с регистром) | точный поиск (+ заголовки animgrp.dat) | inline strcmp 0x5BE7D9 по GetAnimGroupName 0x4D3A20; неизвестная → индекс 0 «default»: нет анимаций стрельбы/перезарядки |
| WPN-11 | Ошибка | `%` группа не из 21: python pythonbad colt45 colt_cop colt45pro sawnoff sawnoffpro silenced shotgun shotgunbad buddy buddybad uzi uzibad rifle riflebad sniper grenade flame rocket spraycan | список | 0x5BED14 (index−11) в aGunAimingOffsets 0xC8A8A8[21]; группы 0..10 → 0xC8A800..0xC8A8A0 поверх gpCrossHairTexFlight 0xC8A810 / gpCrossHairTex 0xC8A818 / gCrossHair 0xC8A838; ≥32 → внутрь aWeaponInfo |
| WPN-12 | Предупр. | `£` baseCombo не UNARMED/BBALLBAT/KNIFE/GOLFCLUB/SWORD/CHAINSAW/DILDO/FLOWERS (точно) | точное сравнение | 0x61DB30 repe cmpsb с NUL → 4 (UNARMED) |
| WPN-13 | Инфо | `$` animGroup валидна, но индекс не в 11..31 (ваниль: goggles ×2) | индекс | 0x5BEB8C: aimOffsetIndex только при 10<idx<32 |
| WPN-14 | Краш | id 334, 345, 346 не определены ни в одном IDE | id в IDE | LoadInitialWeapons 0x40A120 → RequestModel(0x15A/0x14E/0x159, 2) без NULL (0x408903..0x40890A) |
| WPN-15 | Предупр. | нет `$` со skill 1 для 22 PISTOL, 24 DESERT_EAGLE, 28 MICRO_UZI, 30 AK47 | строки есть | CGangs::Initialise 0x5DE680: id 0x16/0x18/0x1C/0x1E зашиты; иначе defaults Initialise (MELEE, модель −1) |
| WPN-16 | Инфо | типы 22..32 без skill-строки 0 или 2 (3 COP опциональна) | строки на тип | GetWeaponInfo 0x743C60: строка = type+25/+0/+36/+47 |
| WPN-17 | Ошибка | ammoClip/damage вне int16; numCombos вне 0..255 | диапазон | усечение при записи 0xC8AAD8 / 0xC8AADA / 0xC8AB27 |
| WPN-18 | Ошибка | flags не hex | `[0-9A-Fa-f]+` | `%x` останавливается на первом не-hex → частичные флаги, дальнейшие поля не разобраны |
| WPN-19 | Инфо | дубликаты `$` (тип+skill) / `£`; `£` для стрелкового типа или `$` для melee | учёт | последняя строка побеждает; `$` для melee форсирует skill 1 |
| WPN-20 | Инфо | строки после ENDWEAPONDATA или с неизвестным первым байтом | — | 0x5BEC76 strncmp ENDWEAPONDATA 13 |
| WPN-21 | Предупр. | range ≤ 0 для не-melee; accuracy или moveSpeed ≤ 0 | диапазон | множители в рантайме; заголовок файла: 0.5..2.0 / 0.5..1.5 |

---

## 5. Педы — ped.dat, pedstats.dat, pedgrp.dat, popcycle.dat, animgrp.dat, peds.ide, decision, surfaces, shopping, stats (PDD-*)

Ваниль: ped.dat 17 блоков типов; pedstats 43 строки; pedgrp 57 групп, CRLF; popcycle 480; animgrp 21 группа; peds.ide 276; PedEvent 41 (7..79); allowed 273 строк по 80 чисел; surfinfo/surfaud по 179; shopping 544 items; statdisp 122; ar_stats 59. Режимы открытия важны: weapon.dat/popcycle/animgrp/statdisp/ar_stats — `rb` + LoadLine; ped.dat/pedstats/pedgrp/surf*/shopping/PedEvent/allowed — `r` (текстовый, CRLF → LF).

| id | Уровень | Условие | Как проверить | Движок |
|---|---|---|---|---|
| PDD-01 | Краш | ped.dat отсутствует | существование | LoadPedData 0x608B30 (LoadLine 0x608B4A) |
| PDD-02 | Краш | Hate/Dislike/Like/Respect до первой строки типа или после строки с именем не из 32 | конечный автомат: валидный индекс типа перед каждой acquaintance-строкой | индекс стартует 0x20 (0x608B3C); FindPedType 0x608790 → 32; запись DAT_00C0BBE8 + 32*0x14 + {0x10,0xC,4,0} за блоком new(0x284) (округлён до 0x288: Respect (+0) — в слак, Like/Dislike/Hate портят заголовок следующего heap-блока) |
| PDD-03 | Предупр. | имя в acquaintance-списке не из 32 | список | вклад бита 0 |
| PDD-04 | Краш | первый токен строки ≥ 32 | длина | sscanf %s в local_20[32] 0x608B5A |
| PDD-05 | Инфо | блок типа дважды / player-типы перечислены | — | каждая строка заменяет маску |
| PDD-06 | Краш | pedstats.dat отсутствует | существование | LoadPedStats 0x5BB890 |
| PDD-07 | Краш | > 43 строк данных | счёт не-#, непустых | iVar4 += 0x34 без проверки; блок new(0x8BC) = 43×0x34 |
| PDD-08 | Краш | токен имени ≥ 32 | длина | sscanf %s local_20[32] |
| PDD-09 | Ошибка | имя 24..31 | ≤23 | strcpy в 24-байтовое поле +4, +0x1C затирается fleeDist → GetPedStatType 0x6088D0 не найдёт |
| PDD-10 | Предупр. | < 43 строк или имена/порядок отличаются от ванильных 43 | список §3.2 отчёта | индексация по позиции (STAT_PLAYER 0, STAT_COP 1…); неизвестное → 16 |
| PDD-11 | Ошибка | fear/temper/lawfulness/sexiness вне 0..255; shootingRate вне int16; defaultDecisionMaker вне 0..7 (**+0x32 читается как signed char**: 128..255 → отрицательные DM, −1/−2 спецзначения) | диапазоны | u8/int16 записи +0x24..+0x27, +0x30, +0x32; SetPedDefaultDecisionMaker 0x5E06E0 |
| PDD-12 | Ошибка | < 11 полей | счёт | `%s %f %f %d %d %d %d %f %f %d %d` |
| PDD-13 | Краш | pedgrp.dat отсутствует | существование | LoadPedGroups 0x5BCFE0 |
| PDD-14 | Ошибка | > 57 групп | счёт | m_PedGroups 0xC0F358 (57×21×2 = 0xC0FCB2 = m_bDontCreateRandomGangMembers..CurrentWorldZone 0xC0FCBC); группа 60 → счётчик на 0xC0ED38 |
| PDD-15 | Предупр. | < 57 групп | счёт | поздние ePopcyclePedGroup пусты |
| PDD-16 | Предупр. | токен не модель ни в одном IDE (без регистра) | поиск | GetModelInfo NULL → пропуск 0x5BD0F0 |
| PDD-17 | Краш (с оговоркой) / Ошибка | токен — модель не из peds (машина/объект/оружие) | секция-определитель | id хранится без проверки типа; CPopulation::AddPed читает CPedModelInfo (m_pHitColModel +0x34…) — не прослежено инструкционно, отчёт §13 |
| PDD-18 | Предупр. | > 21 токена до `#`/конца | счёт | цикл `< 0x15` |
| PDD-19 | Ошибка (**понижено**) | токен ≥ 256 или строка ≥ 1024 | длины | strncpy ≤1023 байт в cStack_500[256] — переполнение остаётся внутри abStack_400 того же фрейма; строка ≥1024 режется fgets на 2 записи → лишняя группа, сдвиг индексов |
| PDD-20 | Предупр. (**понижено**) | последняя строка без `\n` | последний байт 0x0A | скан 0x5BD02A ищет 0x0A мимо NUL — обычно останавливается на старом 0x0A; за стек выходит только если последняя строка самая длинная / единственная, и портит лишь байты 0x2C/0x0D |
| PDD-21 | Предупр. (**понижено**) | gang-группа (42..49) резолвится в 0 моделей | счёт | ChooseGangOccupation 0x611550 проверяет `0 < count` → банда просто не спавнится |
| PDD-22 | Краш | popcycle.dat отсутствует | существование | CPopCycle::Initialise 0x5BC090 |
| PDD-23 | Краш | < 480 строк данных (не `/`, непустые) | счёт | цикл iStack_6c += 0x12 < 0x168; LoadLine NULL на EOF → sscanf(NULL) 0x5BC1E4 → strlen(NULL) |
| PDD-24 | Инфо | > 480 | счёт | лишние не читаются |
| PDD-25 | Ошибка | < 24 чисел в строке | 24 ведущих int | недостающие = старые/мусор |
| PDD-26 | Ошибка | значение вне 0..255 | диапазон | байтовые записи 0x5BC1E9..0x5BC30B |
| PDD-27 | Ошибка | все 18 процентов групп = 0 | сумма полей 7..24 > 0 | 100/sum (0x858628, FDIVR 0x5BC38D) → все 0, max-скан берёт индекс 17 → 100 % Aircrew_runway |
| PDD-28 | Инфо | порядок блоков зон отличается от ванили | не решается оффлайн | позиционные циклы |
| PDD-29 | Краш | animgrp.dat отсутствует (есть: DAT-03) | — | 0x5BC910 |
| PDD-30 | Краш | заголовок группы < 4 токенов или count не int (сейчас ANIMGRP = Предупреждение — **поднять**) | 4 токена, int | AddAnimAssocDefinition 0x4D3BA0 new(count*4)/new(count*0x18) с мусорным count |
| PDD-31 | Ошибка (**понижено**) | имя группы или блока ≥ 16 | ≤15 | strcpy в 16-байтовые поля +0/+0x10 выполняется **до** записи modelIndex/count/указателей (они перезаписываются верно); имя теряет NUL → lookup из weapon.dat/peds.ide/IFP не совпадает → далее краш по PDD-39; ≥48 (группа) / ≥32 (блок) залезают в следующий слот |
| PDD-32 | Ошибка / Краш (уточнено) | любой токен ≥ 32 → Ошибка; Краш: 3-й токен заголовка ≥ 32, блок ≥ 64, имя группы / строка анимации ≥ 96 | длины | фрейм 0x5BC910: acStack_60[32] (1-й токен/анимация), auStack_40[32] (блок), auStack_20[32] (3-й токен) |
| PDD-33 | Краш | строк анимаций в группе больше объявленного count | счёт до end | AddAnimToAssocDefinition 0x4D3C80 ищет первый пустой слот без границ; **PDD-35 (имя ≥24) сдвигает слот и даёт тот же краш при точном count** |
| PDD-34 | Предупр. | строк меньше count | счёт | пустые слоты, позиционные id сдвигаются |
| PDD-35 | Ошибка | имя анимации ≥ 24 | ≤23 | 24-байтовые слоты (new(count*0x18)); см. PDD-33 |
| PDD-36 | Краш / Инфо (уточнено) | count 0 **и** ≥ 1 строка анимации → Краш; count 0 и сразу end → Инфо | | new(0) валиден; AddAnimToAssocDefinition читает *puVar2 (мусор кучи) и strcpy через него |
| PDD-37 | Ошибка | нет `end` перед следующим заголовком/EOF | автомат | следующий заголовок съедается как имя анимации |
| PDD-38 | Ошибка (порча .data) | > 27 групп | счёт | 146-я запись → 0x8AA5A8+145*0x30 = 0x8AC0F8 — другая таблица .data (8, 0x15, 0x16…), не гарантированный AV |
| PDD-39 | Краш | имя блока не IFP ни в одном IMG, или анимация отсутствует в этом IFP | кросс-ссылка (частично есть: DAT-12/IFP) | CreateAssociations 0x4CE6E0 → NULL hierarchy (ifp_path 3.10/3.12) |
| PDD-40 | Ошибка | peds.ide pedType не из 32 (с регистром) | список | LoadPedObject 0x5B74F2 → FindPedType → 32; GetPedFlag(32) 0x608830 → 0 |
| PDD-41 | Предупр. | statName не из первых 43 строк pedstats (с регистром) | список | GetPedStatType → 16 (STAT_SENSIBLE_GUY) |
| PDD-42 | Предупр. | voiceType не PED_TYPE_GEN/EMG/PLAYER/GANG/GFD/SPC | список | GetAudioPedType 0x4E3C60 (0x8C8108) → 0xFFFF → немой |
| PDD-43 | Инфо (**обязательно**) | voice1/voice2 нет в таблице типа (GEN 209 @0x8AE6A8, EMG 46 @0x8BA0D8, PLY 20 @0x8BBD40, GNG 52 @0x8BE1A8, GFD 18 @0x8C4120; шаг 20) | список | GetVoice 0x4E3CD0 → −1; ваниль: WMYSGRD, BMYMIB, BMYPIMP |
| PDD-44 | Ошибка | radio1/radio2 вне −1..254 | диапазон | radio+1 в u8 +0x38/+0x39 |
| PDD-45 | Ошибка / Краш (**уточнено, размеры из 14 push 0x5B742A**) | Ошибка при превышении слота: animFile ≥16, modelName ≥24, voiceType ≥20, statName ≥24, animGroup ≥24, pedType ≥24, txd ≥24, voice1 ≥60, voice2 ≥60. **Краш** (до адреса возврата): voice2 ≥60, voice1 ≥120, txd ≥144, pedType ≥168, animGroup ≥192, statName ≥216, voiceType ≥236, modelName ≥260 | длины по полям | фрейм sub esp,0x128 + 4 push, ret на E+0x138; слоты: animFile E+0x24, modelName +0x34, voiceType +0x4C, statName +0x60, animGroup +0x78, pedType +0x90, txd +0xA8, voice1 +0xC0, voice2 +0xFC |
| ~~PDD-46~~ | выброшено | «имя без B/W/H/I/O в первых двух символах» — ~60 ванильных педов получают race 0 | | FindPedRaceFromName 0x5B6D40 |
| PDD-47 | Краш | PedEvent.txt отсутствует | существование | 0x5BB9F0 (ReadLine 0x5BBA4A) |
| PDD-48 | Ошибка (**понижено**) | > 41 события | счёт | индекс n ≥ 41 → .ped-загрузчик пишет CDecision::Set по dm+n*0x3C = 0x3C байт на лишнее событие в следующий CDecisionMaker внутри того же объекта (0xF1C0) — ограниченная порча, AV только при сотнях событий |
| PDD-49 | Краш | номер события вне 0..95 | диапазон | 0x5BBA85 `mov [ebx+edx*4],esi` в m_EventIndices[96] @+0xC034 без проверки |
| PDD-50 | Краш / Предупр. (уточнено) | строка **без числа** (в т.ч. строка из пробелов или комментарий — пропускаются только строки с первым байтом 0 или `\n`) → **Краш** (ev со стека → дикая запись, n всё равно ++); строка > 255 символов → Предупреждение (ReadLine 0x100 режет, хвост = ложная запись) | парсинг `%s %d`; файл текстовый — CRLF-пустые строки безопасны | sscanf 0x5BBA79 |
| PDD-51 | Предупр. | нет одного из 15 файлов: RANDOM.ped, m_norm.ped, m_plyr.ped, RANDOM.grp, MISSION.grp, GangMbr.ped, Cop.ped, R_Norm.ped, R_Tough.ped, R_Weak.ped, Fireman.ped, m_empty.ped, Indoors.ped, RANDOM2.grp | существование | LoadDecisionMaker 0x6076B0 NULL-проверен (0x6076E4) → DM пустой |
| PDD-52 | Ошибка | строка данных .ped/.grp ≠ 80 чисел через запятую, или > 255 символов (ReadLine 0x200 в отчёте — верификация: 0x100 для PedEvent; для .ped 0x200) | split по `,` | один sscanf на 80 конверсий 0x6079B4 (add esp,0x148) |
| PDD-53 | Краш | id события (первое число) вне 0..95 | диапазон | 0x607A1D `mov ecx,[eax+esi*4+0xC034]; imul 0x3C` без проверки → CDecision::Set 0x6006B0 куда угодно |
| PDD-54 | Инфо (**обязательно**) | событие не перечислено в PedEvent.txt | поиск | m_EventIndices = 0 → слот 0; ваниль: 19 (по верификации; отчёт: 181) таких строк |
| PDD-55 | Краш | surface.dat / surfinfo.dat / surfaud.dat отсутствует | существование | 0x55D111 / 0x55EBB0 / 0x55F2D5 |
| PDD-56 | Ошибка (уточнено) | surface.dat > 6 строк данных; строка с именем без пробела после него | счёт | LoadAdhesiveLimits 0x55D0E0 читает ровно row+1 значений (лишние игнорируются, недостающие = 0.0); 7-я строка пишет в surface-записи this+0x90 |
| PDD-57 | Предупр. | surface.dat < 6 строк | счёт | остальные лимиты 0 |
| PDD-58 | Ошибка | surfinfo/surfaud: имя не из 179 (с регистром) | список (DEFAULT..RAILTRACK) | GetSurfaceIdFromName 0x55D220 → 0 (DEFAULT получает свойства строки) |
| PDD-59 | Ошибка | surfinfo: < 36 полей или нечисловые int | счёт | sscanf 0x55ECD4 (6 + 29 int + 1) |
| PDD-60 | Предупр. | adhesionGroup не RUBBER/HARD/ROAD/LOOSE/SAND/WET; skidmark не DEFAULT/SANDY/MUDDY; friction не NONE/SPARKS; bulletFx не NONE/SPARKS/SAND/WOOD/DUST | список | memcmp-цепочки; биты не меняются |
| PDD-61 | Инфо | tyreGrip×10 / wetGrip×100 вне байта после ftol (ваниль wetGrip −0.25/−0.40 → 0xE7/0xD8) | заметка | 0x85862C / 0x858628 |
| PDD-62 | Ошибка / Краш (уточнено) | Ошибка: name ≥ 64 (оба файла), adhesion/skidmark/friction/bulletFx ≥ 32; **Краш**: name ≥ 64, friction ≥ 96, skidmark ≥ 128, bulletFx ≥ 160, adhesion ≥ 192 | длины | surfinfo 0x55EB90: name local_40[64], adhesion local_c0[32], skidmark local_80[32], friction local_60[32], bulletFx local_a0[32]; surfaud 0x55F2B0 name local_40[64] |
| PDD-63 | Инфо | зашитая поверхность без строки | покрытие | таблицы нули |
| PDD-64 | Краш | shopping.dat отсутствует | существование | CShopping::LoadStats 0x49B6A0 → FindSection 0x15659EB LoadLine(NULL) |
| PDD-65 | Предупр. | нет верхней `section prices` | структура | ничего не загружено |
| PDD-66 | Ошибка | всего items под prices > 560 | счёт | ms_numBuyableItems без границ; ms_keys 0xA97D90[560] → ms_priceModifiers 0xA98650 |
| PDD-67 | Ошибка | подсекция prices > 300 items или один shop > 300 item-строк | счёт | ms_prices 0xA986F0 (+300×0x18 = ms_numBuyableItems 0xA9A310); ms_shopContents 0xA9A318 (+300×4 = ms_priceSectionLoaded 0xA9A7C8) |
| PDD-68 | Краш | `type <name>` в shop не из None/CarMods/CarPaintJobs/Furniture/Clothes/Haircuts/Tattoos/Gifts/Food/Weapons/Property (без регистра) | список | GetPriceSectionFromName 0x49AAD0 → −1 → ms_sectionNames[−1] = dword 0x8A61D4 = 0x0000FFFF → _stricmp по адресу 0xFFFF (а до того — путь PDD-69) |
| PDD-69 | Краш (**поднято**) | `type` называет price-секцию, отсутствующую под prices | кросс-ссылка | FindSection 0x15659E0 доходит до `end` блока prices (depth<0) и оставляет позицию за ним; LoadPrices 0x49B8D0 читает следующие строки как items: `section shops` → strtok… `_atol(strtok(NULL)=NULL)` 0x49BAA4 → atol 0x822593 `movzx eax,[esi]` по NULL. Только если за prices идут лишь комментарии/EOF — игнорируется |
| PDD-70 | Ошибка / Краш | `item` до `type` в shop → ключ с предыдущей/None секцией; `type` без второго токена → strcpy(NULL) → **Краш** | порядок, токены | LoadShop 0x49BBE0 (memcmp 5 байт, с регистром) |
| PDD-71 | Предупр. | `item` нет в соответствующей подсекции prices | ключ | нет цены |
| PDD-72 | Ошибка | nametag > 7 | длина | strncpy(tag,8) 0x49BA0A; ItemPrice 0x18, тег +0x10 без NUL → въезжает в ключ следующего |
| PDD-73 | Ошибка | item в CarMods/Furniture/Gifts/Food/Property/неизвестной подсекции не имя модели в IDE | поиск | GetModelInfo → −1; все нерезолвленные делят ключ −1 |
| PDD-74 | Ошибка | Weapons item не имя типа оружия | список WPN | FindWeaponType → 0 = UNARMED |
| PDD-75 | Предупр. | stat не fat/respect/sexy/health/stamina/calories/`-` (точно) | список | 0x1565360 → −1 |
| PDD-76 | Ошибка | stat change вне −128..127; price/ammo/type не int | диапазон | atol → int8 0x49B7A1.. |
| PDD-77 | Краш | item-строка с нехваткой токенов: LoadPrices — 7 общих / 9 Clothes,Haircuts,Tattoos / 8 Weapons; **также на старте в LoadStats** (0x49B79x): 8 для Clothes/Haircuts/Tattoos, 7 Weapons, 6 остальные | счёт по подсекции | strtok NULL → atol(NULL) / GetUppercaseKey(NULL) 0x49BAB2; repe cmpsb по NULL |
| PDD-78 | Ошибка | подсекция CarPaintJobs с items | наличие | 0x49B72B ключ не присваивается, GetKey возвращает константу 2 |
| PDD-79 | Краш / Предупр. (уточнено) | токен `type` ≥ 32 → Краш; имя shop-секции ≥ 24 → Предупреждение (не совпадёт с FindSection — магазин пустой; strcpy в ms_shopLoaded копирует аргумент скрипта, не файл) | длины | local_20[32] в 0x49BBE0 |
| PDD-80 | Предупр. | Clothes/Haircuts modelname нет в player.img; Tattoos texture нет в tattoo-TXD | каталог player.img | хэш не совпадает — предмет невидим (см. CLO-03: если имя вообще не в player.img — Краш) |
| PDD-81 | Ошибка | дисбаланс section/end | скобки | FindSection depth 0x15659E0 |
| PDD-82 | Краш | statdisp.dat / ar_stats.dat отсутствует | существование | 0x5598A5 / 0x5599E0 |
| PDD-83 | Ошибка | statdisp > 128 строк | счёт | 0x559977 ebp += 0x10; StatMessage 0xB78200..0xB78A00 → LastMissionPassedName |
| PDD-84 | Ошибка | gxt-метка (5-й токен) > 7 | длина | strcpy в 8 байт entry+8 (0x559960); ≥8 сначала портит буфер condition → всегда lessthan |
| PDD-85 | Предупр. | condition не ровно lessthan/morethan | точно | memcmp 9 байт 0x55991F/0x55993F → lessthan |
| PDD-86 | Ошибка (**диапазон уточнён**) | statId не в **0..81 или 120..343**; < 5 токенов; value не float | диапазон/счёт | GetStatValue 0x558E40: `cmp ax,0x52; jae` → float-таблица 0xB79380 для 0..81; иначе `fild [0xB78E20+id*4]` = StatTypesInt[id−120], таблица 0xB79000..0xB79380 = 224 int; value/id предобнулены (не мусор) |
| PDD-87 | Краш / Предупр. (уточнено) | statName ≥ 80 → Краш (EBP/ret); condition ≥ 12 → Предупреждение (только в statName, который не используется), ≥ 92 → Краш | длины | фрейм 0x559860: gxt E+0x24[8], condition +0x2C[12], statName +0x38[80] |
| PDD-88 | Ошибка | ar_stats id вне 0..59 | диапазон | 0x559A1D StatReactionValue[id] (0xB78F10..0xB79000) |
| PDD-89 | Краш | ar_stats имя ≥ **80** (уточнено, не 76) | длина | 0x5599B0: name E+0xC, ret E+0x5C |
| PDD-90 | Инфо | ids 0..58 не все присутствуют | покрытие | таблица 0 |

---

## 6. Одежда CJ — player.img / clothes.dat / shopping.dat (CLO-*)

Все CLO подтверждены. «Краш» для путей с неинициализированным offset/size (CLO-03/04, CUT-07) = **краш или зависание** (CdStreamRead за EOF → цикл повтора стриминга либо NULL m_pRwObject).

| id | Уровень | Условие | Как проверить | Движок |
|---|---|---|---|---|
| CLO-01 | Краш | player.img не VER2 или > 550 записей | заголовок, счёт (ваниль 542) | CDirectory::Init(0x226) 0x5A4190; ReadDirFile 0x5323BA `cmp count,cap; jge` → запись выброшена → потом CLO-03/04 |
| CLO-02 | Краш | имя записи ≥ 24 (без NUL); streaming-size (байты 4-5) = 0; размер > наибольшей записи стрим-IMG, **округлённой до чётного числа секторов** (ваниль 385 < 1264) | скан каталога | GetUppercaseKey 0x53CF30 читает до NUL; FindItem 0x5324D0; player.img не поднимает ms_streamingBufferSize (Init2 0x5B8E20: `test al,1; jz; inc eax`) |
| CLO-03 | Краш/зависание | имя модели части (defaults torso/head/hands/legs/feet, SETC arg3, CUTS arg2, shopping Clothes/Haircuts modelname) без `<NAME>.DFF` в player.img | собрать имена из обоих .dat + defaults, искать без регистра | ConstructGeometryArray 0x5A55A0: out-параметры FindItem в ESP+0x14/0x18 не переинициализируются (слоты 1..9 берут offset/size **предыдущей** части, слот 0 — мусор); RequestGeometry 0x5A41C0 при промахе уже выдал RequestFile(384+i, off=hash&0xFFFFFF, size=385..393) — чтение далеко за EOF |
| CLO-04 | Краш | имя текстуры (player_torso/legs/face/feet, SETC arg4, TEX arg2, shopping texturename) без `<NAME>.TXD` | тот же набор | RequestTexture 0x5A4220 — свежие локалы → мусор → словарь NULL → RwTexDictionaryFindNamedTexture 0x7F39F0 [NULL+8] / GetFirstTexture(NULL) → CopyTexture 0x5A5735 `mov ebx,[eax]` |
| CLO-05 | Краш | (уточнено) объединение атомиков по **всем** верхнеуровневым CLUMP-чанкам записи не содержит фреймов normal/fat/ripped (stricmp, первый матч), или какой-то RpClumpStreamRead проваливается | разбить запись по CLUMP (0x10), проверить имена фреймов атомиков (ваниль: всегда 3 клампа × 1 атомик) | LoadClumpFile 0x5372D0 путь флага 2; GetAtomicWithName 0x5A4810 → 0; BlendGeometry 0x5A4940 читает *(0+0x18) |
| CLO-06 | Краш | три геометрии различаются числом вершин, или у какой-то нет нормалей / UV0 / Skin PLG | сравнить по клампам | BlendGeometry цикл по normal->numVertices читает +0x14/+0x18/+0x34 и RpSkinGetVertexBoneIndices всех трёх без проверок; меньший fat/ripped → чтение за кучу |
| CLO-07 | Краш | нет HAnim на фрейме атомика/поддереве; > 64 нод; индекс кости ≥ numNodes; атомики не скинены | HAnim PLG с numNodes>0; max skin index < numNodes ≤ 64 (**для части 9 extra1 > 64 нод пишет поверх каталога player.img**) | SetClump 0x4C4F70 → GetAnimHierarchyFromFrame 0x734AB0; StoreBoneArray 0x5A48B0 (hierarchy+4 по NULL); BuildBoneIndexConversionTable 0x5A56E0 local_40[64] |
| CLO-08 | Ошибка | сумма вершин normal-клампов 10 надетых частей > 65535 | флаг любой части > 6000 вершин (ваниль макс 734) | ConstructGeometryAndSkinArrays 0x5A6530: (short)base + u16 индексы |
| CLO-09 | Краш | player_torso.txd без torso/torso_fat/torso_ripped; player_legs.txd без legs/legs_fat/legs_ripped; пустые face/feet; размеры fat/ripped ≠ base | имена + размеры четырёх базовых TXD | 0x5A6111.., BlendTextures 0x5A59C0/0x5A5BC0/0x5A5820 (граница цикла — растр **_fat**, 4 Б/пиксель) |
| CLO-10 | Ошибка | растр не 32bpp или DXT/PAL; overlay в torso-слоте ≠ 256×256; legs ≠ 128×256; tattoos ≠ 256×256 | depth==32, нет fourcc/палитры; роль из shopping type / clothes component | PlaceTextureOnTopOfTexture 0x5A57B0; циклы по w×h источника через RwRasterLock: сжатый/больший источник — переполнение кучи |
| CLO-11 | Краш | TXD в player.img без текстур | count ≥ 1 | GetFirstTexture → NULL → CopyTexture |
| CLO-12 | Предупр. | строки правил вне rule..end; строка в блоке, начинающаяся со строчного `end` (endignore/endexclusive закрывают блок) | сканер; ваниль пишет ENDIGNORE/ENDEXCLUSIVE прописными | LoadClothesFile 0x5A7B30 strncmp('rule',4)/('end',3) с регистром |
| CLO-13 | Краш | SETC < 4 аргументов, CUTS/TEX/HIDE < 2, IGNORE/ENDIGNORE/EXCLUSIVE/ENDEXCLUSIVE < 1; неизвестное ключевое слово → Ошибка (наследует тип предыдущего) | счёт токенов | strtok NULL → GetUppercaseKey 0x53CF36 `mov al,[edi]` |
| CLO-14 | Ошибка | компонент SETC/HIDE не ровно torso head hands legs feet necklace watch glasses hat extra1 (строчные) | точно | GetClothesModelFromName 0x5A7A20 → 0 (torso) |
| CLO-15 | Ошибка | всего слов правил (SETC 5, CUTS/TEX/HIDE 3, прочие 2) > 600 (ваниль 587; реальная порча с 607 = PlayerClothes 0xBC1C78, с 636 g_bCutSceneFinishing) | счёт | ms_clothesRules 0xBC1300, DAT_00bc12fc++ без границ |
| CLO-16 | Предупр. | > 8 одновременно активных IGNORE/EXCLUSIVE | симуляция стека (ваниль макс 5) | PreprocessClothesDesc 0x5A44C0 local_20[8] |
| CLO-17 | Ошибка / Краш (уточнено) | Clothes/Haircuts type не в {0,1,2,3,13..17}; Tattoos type не в 4..12 → **Краш** (4-й токен `-` хэшируется в models[GetTextureDependency(type)] → CLO-03); nametag > 7; имена > 19 | parse prices → Clothes/Haircuts/Tattoos | LoadPrices 0x49B8D0 case 6: DAT_00a986fc = GetUppercaseKey(tok4); SetTextureAndModel 0x5A8050 textures[18] по type без проверки |
| CLO-18 | Краш | gta3.img: player.dff или csplay.dff отсутствуют/не скинены; player.txd отсутствует (→ модели 0 назначается `generic`, и каждый ребилд уничтожает все текстуры generic.txd) | наличие + skin | CreateSkinnedClump 0x5A6C6A..; RequestSpecialModel 0x409F83..; 0x5A69D0 RwTexDictionaryForAllTextures(destroy) |
| CLO-19 | Предупр. | anim.img без IFP-блоков fat / muscular | список IFP | RequestMotionGroupAnims 0x5A8120: −1+0x63E7 = 25574 = DAT-слот 63 (безвредно) |
| CLO-20 | Инфо | models/generic/player.bmp, skins/*.bmp нечитаемы | опционально | GetSkinTexture 0x6FFA86 терпит NULL |

## 7. Катсцены — cuts.img / .cut / .ifp / .dat / cutscene.img (CUT-*)

| id | Уровень | Условие | Как проверить | Движок |
|---|---|---|---|---|
| CUT-01 | Предупр. | cuts.img не VER2; > 512 записей; имя ≥ 24; streaming size 0 (для .cut → new(0) и парсинг мусора кучи до первого NUL; для .ifp/.dat безвредно) | каталог (ваниль 444) | CCutsceneMgr::Initialise 0x4D5A20 CDirectory(0x200); ReadDirFile 0x532350 |
| CUT-02 | Краш / Инфо (**порог уточнён**) | базовое имя катсцены **> 11** символов → Краш-likely (затирается ms_pCutsceneObjects[0] @0xBC3F18); 8..11 → Инфо (0xBC3F10/0xBC3F14 не используются как переменные) | длина basename каждого .cut (ваниль ≤7) | LoadCutsceneData_overlay 0x5B13F0 побайтовая копия в ms_cutsceneName[8] 0xBC3F0C без границы |
| CUT-03 | Предупр. | NAME.cut без NAME.ifp / NAME.dat (и наоборот); без .ifp остаются ассоциации предыдущей катсцены (возможный use-after-free) | группировка по basename | _preload 0x5B05A0; _postload 0x5AFBC0; HasCutsceneFinished 0x5B0570 |
| CUT-04 | Краш | строка ≥ 1024; нет NUL внутри записи; заголовок секции не ровно info/model/text/uncompress/attach/remove/peffect/extracol/end | эмуляция читателя; неизвестные заголовки | читатель 0x5B0830..0x5B087A (1024-байтовый стек без границы); repe cmpsb 0x5B0867..0x5B098B |
| CUT-05 | Краш | нет корректной строки `offset X Y Z` | одна парсящаяся строка | 0x5B09E4.. sscanf `%f %f %f` не проверен → мусор в LoadScene |
| CUT-06 | Краш | model: строка < 2 токенов; строка без anim-токена (молча выброшена — Предупр.); слотов > 50; имя ≥ 32 | счёт (ваниль макс 18) | 0x5B0B04..0x5B0C41 strcpy из strtok NULL 0x5B0B40; массивы [50] 0xBC38C8/0xBC3288/0xBC31C0; 51-й затирает ms_cutsceneTimer/ms_cutsceneName 0xBC3F08 |
| CUT-07 | Краш/зависание | не-IDE и не-csplay модель без `<name>.dff` в стрим-IMG; > 20 special-моделей на катсцену; > 550 DFF без IDE во всех IMG | резолв имён (ваниль макс 16 / 395) | 0x5B10DE.. цикл IsModelLoaded; RequestSpecialModel 0x409F76 результат игнорируется; CreateCutsceneObject 0x5B02D0; 21-я special затирает IDE-модель 320+ |
| CUT-08 | Предупр. | special-модель без одноимённого .txd | пары DFF/TXD (ваниль 317/317) | RequestSpecialModel 0x409F83 FindTxdSlot → `generic` |
| CUT-09 | Краш / Ошибка | text: не `start,duration,LABEL` с запятыми; > 64 строк → Краш; метка > 7 → Ошибка (следующий слот); ≥ 24 → Краш (курсор строки esp+0x34; 20..23 портят только extracol) | regex + счёт (ваниль макс 58) | 0x5B0C46.. sscanf `%d,%d,%s`; ms_cTextOutput[64][8] 0xBC2FC0; 65-я → ms_iModelIndex |
| CUT-10 | Краш | uncompress: строка **только из пробелов** (полностью пустые пропускаются читателем); > 7 имён (8-е обнуляет младший байт ms_iTextDuration[0] терминатором); имя ≥ 32; имя не в IFP → Предупр. | счёт + IFP (ваниль макс 5) | 0x5B0CFC.. strtok → LoadAnimationUncompressed 0x4D5AB0; [8][32] 0xBC2CC0 |
| CUT-11 | Краш | attach: не `objA,objB,boneId`; > 50; objA/objB ≥ числа слотов (0-based); boneId не в иерархии objA; **objA — жёсткая (нескиненная) модель** → GetAnimHierarchyFromSkinClump 0x734A40 = 0 → RpHAnimIDGetIndex(NULL) | индексы vs CUT-06, id костей vs HAnim DFF | _loading 0x5B11C0; ms_iAttachObjectToBone[50] 0xBC2A68 |
| CUT-12 | Ошибка | remove: не `x,y,z,modelname`; > 50; имя ≥ 32; модель не в IDE (пропуск в рантайме) | regex + IDE | 0x5B0D7F..; [50] → ms_pHiddenEntities |
| CUT-13 | Краш | peffect: < 11 полей name,start,end,objId,part,x,y,z,dx,dy,dz; > 8 строк; objId вне 1..слоты; эффект не в effects.fxp → Предупр. | счёт; имена vs `NAME:` в fxp | 0x5B0E08.. strncpy/atoi/atof по NULL; ms_pParticleEffects[8] 0xBC1D70; 9-й = ms_crToHideItems |
| CUT-14 | Ошибка | extracol вне **0..16** (0 валиден: `test eax,eax; je` 0x5B09C2); больше одной строки → Инфо | диапазон | StartExtraColour 0x55FEC0 (v−1)/8+21, (v−1)&7; отрицательные → слот < 0 |
| CUT-15 | Краш / Предупр. / Инфо | имя блока в .ifp ≠ basename катсцены → **Краш** (GetAnimationBlock 0x4D3940 NULL → `mov edi,[eax+0x18]` 0x4CE40D); анимация из .cut отсутствует в IFP → Предупр. (статичный объект); корневая последовательность без translation или с 0 кадрами → Ошибка/Краш; имя > 23; **анимаций в IFP больше, чем anim-токенов в .cut → Инфо** (ваниль: 15 таких IFP, цикл 0x4CE4C7 сравнивает со старыми слотами) | парс IFP + кросс-чек с CUT-06 (ваниль 148/148, 974/974 корней с translation) | CreateAssociations 0x4CE3B0; SetCutsceneAnim 0x5B0390; SetupCutsceneToStart 0x5B14D0 читает keyframe0 +0x14/+0xA; неизвестное имя объекта проверяется (je 0x4CE590) — не краш |
| CUT-16 | Ошибка | .dat не ровно 4 блока `N`, N строк, `;`; блоки 0-1 ≠ 4 чисел, 2-3 ≠ 10; нечисловой count → n=0, пустой блок (Ошибка, не мусор); лишние токены → переполнение кучи (Краш-likely); < 4 блоков — читатель уходит в следующую запись IMG | эмуляция парсера (ваниль 148/148 = 4/4/10/10) | CCamera::LoadPathSplines 0x5B24D0..0x5B25EB malloc n*16+4 / n*40+4, atof-цикл без границ |
| CUT-17 | Краш | cutscene.img: DFF не проходит PED-C*/DFF-*, TXD не проходит TXD-*, имя ≥ 24 | прогнать существующие чекеры по IMG | ConvertBufferToObject 0x40C6B0 → LoadClumpFile → SetClump 0x4C4F70 (special ids 300..319) |
| CUT-18 | Инфо | ADD_CUTSCENE_HEAD — заглушка | — | AddCutsceneHead 0x5B0380 = xor eax,eax; ret |
| CUT-19 | Краш | default.ide без `hier 1, csplay, player`; csplay.dff отсутствует/не скинен; player.txd отсутствует; цель CUTS cs_*.dff нет в player.img | наличие | LoadCutsceneData 0x4D5E80 RequestModel(1) → RebuildCutscenePlayer 0x5A8270 → CreateSkinnedClump 0x5A69D0 |

---

## 8. GXT / main.scm / script.img / IMG / stream.ini / UI (GXT-*, SCM-*, IMG-*, UI-*)

Ваниль: 5 GXT (заголовок 04 00 08 00, TABL 127×12, MAIN offset 1536, все TKEY%8==0, хэши строго возрастают, CDERROR 0xEF128EC3); main.scm targets 43808/53156/53720/55948, 389 объектов (≤18 символов), 135 миссий, main 194146, largest mission 68439, locals 964, 79 seg3 = 79 .scm; IMG все VER2, байты 6..7 = 0, 4 дубля TXD в gta_int.img, наибольшая запись 1263 секторов (vgwsthiway1.txd), 426 .rrr, 251 COL, 3593 TXD, 190 IPL, 154 IFP.

Хэш GXT = CRC32 (таблица 0x8CD068, init 0xFFFFFFFF, **без** финального xor = JAMCRC) от C-toupper ключа (CKeyGen::GetUppercaseKey 0x53CF30).

| id | Уровень | Условие | Как проверить | Движок |
|---|---|---|---|---|
| GXT-01 | Краш | text\AMERICAN.GXT (выбранный язык: байт 0xBA67CC 0..4 → AMERICAN/FRENCH/GERMAN/ITALIAN/SPANISH) отсутствует; остальные четыре → Предупр. | существование | CText::Load 0x6A01A0: OpenFile не проверен (0x6A0228), Read 0x538950 → fread 0x823521 → _lock_str NULL |
| GXT-02 | Предупр. (**понижено**) | первые 4 байта ≠ 04 00 08 00 | байты | два Read(2) 0x6A0237/0x6A0244 съедают 4 байта и **не сравнивают**; краш решает GXT-03 |
| GXT-03 | Краш (зависание) | от смещения 4 не достижимы верхнеуровневые TKEY и TDAT (MAIN) с size>0, или файл обрезан внутри них | обход чанков от 4 (TABL опционален) | цикл 0x6A0255..0x6A0382 выходит только при обоих флагах; неудачный Read 0x6A0280 → 0x6A0298 со старым заголовком → бесконечный цикл / повторный operator new |
| GXT-03b | Краш (зависание) (**добавлено верификацией**) | любой чанк, кроме TABL/TKEY/TDAT (верхний уровень или внутри mission-таблицы), с size ≥ 0x10000 | размер неизвестных чанков < 65536 | цикл пропуска с 16-битным счётчиком: `inc ebp; movzx ecx,bp; cmp ecx,size; jb` 0x6A037A..; то же в LoadMissionText 0x69FBF0 |
| GXT-04 | Ошибка / Краш (уточнено) | TKEY size % 8 ≠ 0 → **Ошибка** (CKeyArray::Load 0x69F490 читает все size байт в блок (size>>3)*8 — переполнение кучи на size%8, позиция файла остаётся выровненной); TABL size % 12 ≠ 0 → Краш (0x69F670 съедает (size/12)*12 → следующий заголовок мусор); > 32767 записей в таблице → Краш (int16 `dec cx` 0x6A0018, lo/hi int16 в 0x69F570; при 32768 lo = −32768) | size%8, size%12, count ≤ 32767 | |
| GXT-05 | Ошибка | TKEY не отсортирован по возрастанию беззнакового хэша (дубли → Предупр.) | hash[i] < hash[i+1] | BinarySearch 0x69F570 сравнивает uint |
| GXT-06 | Краш | TKEY offset ≥ TDAT size или строка без NUL внутри TDAT | offset < dsize и NUL в TDAT[offset..) | CKeyArray::Update 0x69F540 прибавляет базу без проверки; Get 0x6A0050 отдаёт указатель CFont |
| GXT-07 | Краш | TABL > 200 записей | size/12 ≤ 200 | 0x69F670 пишет this+0x12C+i*12; массив 0x960; 200-я затирает int16 count +0xA8C |
| GXT-08 | Ошибка | имя TABL занимает все 8 байт | NUL в 8 байтах (≤7) | LoadMissionText 0x69FBF0 strlen 0x69FC3A уходит в offset → таблица никогда не грузится |
| GXT-09 | Краш (зависание) | offset mission-таблицы не указывает на name[8] + TKEY + TDAT (size>0, внутри файла) | разобрать каждую не-MAIN запись и применить GXT-04..06 | 0x69FD9A.. Seek/Read не проверены, strncmp имени отброшен 0x69FD6F, выход только по TKEY&&TDAT |
| GXT-10 | Предупр. | LOAD_MISSION_TEXT (opcode 0x054C) с меткой, отсутствующей в TABL (точно, та же длина) | скан SCM по 0x054C | 0x69FC0F.. возврат без загрузки → все ключи таблицы = "" |
| GXT-11 | Предупр. | ключ print-опкода отсутствует в MAIN (или в загруженной mission-таблице) | JAMCRC(toupper) ∈ MAIN ∪ таблица по предыдущему 0x054C | Get 0x6A0050 → 0x6A00B1 sprintf(buf,"") (0x858B54) |
| GXT-12 | Предупр. | текстовая метка скрипта длиннее 7 | байт 7 каждого 8-байтового параметра типа 9 = 0 | ReadTextLabelFromScript 0x463D50 копирует 8 байт без терминатора |
| GXT-13 | Предупр. | в MAIN нет CDERROR (0xEF128EC3) | наличие | 0x6A03AD..0x6A03D8 → пустой m_szCdErrorText |
| GXT-14 | Инфо | кодировка: 8-битные NUL-строки, байты ≥ 0x80 — индексы глифов fonts.txd, теги ~x~; перекодировка 0x69F7E0 режет на 255 и мапит 0x80..0xCF в CP1252, ≥ 0xD0 в `#` | информационно | |
| SCM-01 | Краш | main.scm отсутствует | существование | CTheScripts::Init 0x468D50 (OpenFile 0x468EC9, Read 0x468EDB) |
| SCM-02 | Краш / Ошибка / Предупр. (уточнено) | **Краш**: любой segment-target (int32 по +3 seg0..seg3) ≥ 200000 или таблица выходит за 200000 (seg1+12+count*24, seg2+24+missions*4, seg3+16+rows*28); **Ошибка**: байты 0..2 ≠ 02 00 01 (не проверяются, исполнение с байта 0); **Предупр.**: targets не по возрастанию (движку всё равно) | парсинг четырёх заголовков | Init читает 0x30D40 байт (0x468ECE), memset 269000; читатели 0x156F5A0/0x1565E20/0x470750 идут по цепочке +3 без сравнений |
| SCM-03 | Краш / Ошибка (уточнено) | число имён объектов (u16 seg1+8) > 395 → Краш; имя без NUL в 24 байтах → **Ошибка** (хэш идёт до случайного NUL → индекс −1 → далее SCM-04) | count ≤ 395 (ваниль 389); NUL в имени | 0x156F5EB копирует 24 байта в UsedObjectArray 0xA44B70 ×0x1C без границ; конец 0xA476A4, дальше EntitiesWaitingForScriptBrain 0xA476B0 |
| SCM-04 | Краш | имя object-таблицы (индекс ≥ 1; 0 пустой и пропускается) не модель ни в одном IDE (без регистра) | все имена ∈ множеству моделей IDE (ваниль 388/388) | UpdateObjectIndices 0x486780 → 0x1562660 (−1); RequestModel 0x4087E0 без границ (`lea edi,[id*5]; shl 2; [edi+0x8E4CC6]`) → запись в ms_aInfoForModel[−1] при использовании |
| SCM-05 | Краш | missions (int16 seg2+16) > 200; MainScriptSize > 200000; LargestMissionScriptSize или фактическая миссия (разность смещений) > 69000; смещение миссии ≥ размера файла | поля заголовка и разности (ваниль 135 / 194146 / 68439) | 0x4867C0 → 0x1565E20 (MultiScriptArray 0xA444C8[200]); запуск 0x4899D1/0x489A5A всегда Read(69000) → длинная миссия обрезана и исполняется как мусор |
| SCM-06 | Ошибка | строк seg3 < числа .scm в script.img; имя seg3 отсутствует в script.img (stricmp); базовое имя ≥ 20; размер seg3 < реального размера записи (u32 по +20 — виртуальное смещение, игнорируется) | сравнить seg3 с каталогом .scm | ReadStreamedScriptData 0x470750, RegisterScript 0x4706C0 (j=−1 → запись в this−4 0xA47B5C и 0xA47B46), LoadStreamedScript 0x470840 → 0x1565EC0 |
| SCM-07 | Краш | > 82 записей .scm во всех IMG | count ≤ 82 (ваниль 79) | RegisterScript пишет имя по this+8+i*0x20; 83-я (i=82) → +0xA48 = 0xA485A8 = **CScriptResourceManager** (уточнено: не m_nLargestExternalSize) |
| SCM-08 | Предупр. | «largest number of mission locals» (u32 seg2+20) > 1024 | поле ≤ 1024 (ваниль 964) | LocalVariablesForCurrentMission 0xA48960 (0x400 dword) прямо перед ScriptSpace 0xA49960 → алиас глобалов |
| SCM-09 | Инфо | размер глобальных переменных (seg0 target) отличается от сейва | сообщить seg0 | CTheScripts::Load 0x5D4FD0 |
| SCM-10 | Краш (зависание) | LOAD_TXD_DICTIONARY (0x0390) с TXD, которого нет как models\txd\<name>.txd | скан SCM по 0x0390 | 0x484100.. → CTxdStore::LoadTxd 0x7320B0 `do { RwStreamOpen } while (!stream)` |
| IMG-01 | Краш | > 5 строк IMG в default.dat+gta.dat (2 встроенных + строки + player.img > 8 слотов) | счёт строк IMG без MODELS\GTA_INT.IMG ≤ 5 (ваниль 3) | AddImageToList 0x407610 (ms_files 0x8E48D8 8×0x30, возвращает 0 при переполнении); CClothes::Init 0x5A80D0 регистрирует PLAYER.IMG последним (Init2 0x5BA215) → id 0 → одежда читает смещения player.img из gta3.img |
| IMG-02 | Предупр. | строка IMG после первой строки IPL (латч — локал `xor bl,bl` 0x5B903E **на каждый .dat**: IPL в default.dat запускает Init2 дважды — отдельная опасность) | порядок строк | LoadCdDirectory() 0x5B82C0 вызывается один раз из Init2 на первой IPL (LoadLevel 0x5B924E..) → поздний архив открыт, но каталог не сканирован |
| IMG-03 | Ошибка | путь IMG ≥ 40 символов | strlen < 40 | AddImageToList: копия в name[0x28] без границы → имя следующего слота |
| IMG-04 | Краш / Предупр. (уточнено) | count < 0 (≥ 0x80000000) → зависание (~2^32 итераций); count == 0 → безвредно (пустой архив); 8+count*32 > размер файла → Предупр. (короткий Read(0x20) оставляет предыдущую запись в буфере, она повторно пропускается как уже зарегистрированная; только частично прочитанная последняя даёт одно ложное имя); magic VER2 **никогда не сравнивается** | заголовок | LoadCdDirectory 0x5B6170: `if (count == 0) { CloseFile; return; }`; skip 0x5B6450 |
| IMG-05 | Предупр. (есть частично: имена > 20) | имя без `.` или первая `.` на индексе > 20; расширение (3 символа после первой точки, без регистра) не DFF/TXD/COL/IPL/DAT/IFP/RRR/SCM; нет NUL в 24 байтах | правила имени (база ≤ 20, всего 23) | strchr/`< 0x15`, strnicmp(…,3), name[23]=0 — запись молча пропущена |
| IMG-06 | Инфо (**обязательно**, есть: дубликаты) | одно базовое имя + тип дважды (внутри или между IMG) | сообщить с победителем (порядок: gta3, gta_int, строки gta.dat, затем порядок каталога); ваниль: 4 TXD gta_int.img в тени (barrier, kbmiscfrn1, lawest1, changeme) | хвост LoadCdDirectory 0x5B6449: GetCdPosnAndSize 0x1560E50 (m_nCdSize≠0) → skip 0x5B6450 — первый побеждает |
| IMG-07 | Предупр. | streaming size 0 и size-in-archive 0 | байты 4..5 ≠ 0 | m_nCdSize = 0 → модель «не на диске»; дубликат может занять слот |
| IMG-08 | Краш / Предупр. | байты 6..7 (size in archive) ≠ 0; Краш, если больше максимума байтов 4..5 по всем записям (буфер стриминга считается по 4..5, чтение — по 6..7) | байты 6..7 == 0 (ваниль везде 0) | 0x5B61EC (max по 4..5), 0x5B6465..0x5B6486 (6..7 переопределяют размер чтения) → CdStreamRead переполняет ms_pStreamingBuffer |
| IMG-09 | Инфо (**уточнено**) | буфер стриминга = **наибольшая** запись (байты 4..5) округлённая до чётного ×2048 (не ×2), делится пополам на 2 канала; записи больше половины занимают оба канала | сообщить наибольшую (ваниль 1263 → 1264 всего / 632 на канал) | Init2 0x5B9020: MallocAlign(size<<11), size/=2, buffer[1] = buffer[0] + (size/2)*2048 |
| IMG-10 | Предупр. | > 550 записей .dff, базовое имя которых не модель IDE | счёт (ваниль 395: 78 gta3 + 317 cutscene) | ms_pExtraObjectsDir = CDirectory(0x226); AddItem 0x532310 возвращает при count ≥ cap — special-педы/катсценные модели недостижимы по имени |
| IMG-11 | Краш / Ошибка / Предупр. (уточнено) | .rrr > 475 → **Краш** (затирает NumPlayBackFiles 0x97F630); .rrr не carrecN → Предупр. (номер 850, недостижима/дубликат; streaming id = 25755+индекс независимо от имени); .dat не nodesNN → **Ошибка** (sscanf(name+5) проваливается → остаётся id **предыдущей** записи → регистрация в чужой слот; Краш только если это первая запись первого IMG или N ≥ 64 — 25511+N залезает в IFP 25575+); TXD > 5000, COL > 255, IPL > 256, IFP > 180, SCM > 82 | счёт по типам и имена (кросс-ссылки DAT-15, DAT-39, DAT-41, IFP/COL) | RegisterRecordingFile 0x459F80 → 0x156F110 (StreamingArray 0x97D880 ×16); DAT-ветка 0x5B6390; ms_aInfoForModel **26316** записей (0x8E4CC0..0x9654B0, ×0x14) |
| IMG-12 | Краш | stream.ini отсутствует; value-ключ (memory, devkit_memory, vehicles, pe_*, def_brightness_pal) без значения; Инфо: memory/devkit_memory/vehicles переопределяются Init2 (0x3200000 / 22) | существование; два токена на value-строку | ReadIniFile 0x5BCCD0: OpenFile не проверен → fgets(NULL); второй strtok → atol/atof без проверки |
| IMG-13 | Ошибка | (offset + size) × 2048 > размер файла | диапазон на запись | SetCdPosnAndSize сырые значения; CdStreamRead короткий (реакция стрим-потока не прослежена) |
| UI-01 | Краш (зависание) | нет любого из MODELS\FONTS.TXD, PCBTNS.TXD, HUD.TXD, TXD\LOADSCS.TXD, FRONTEN1/2/3.TXD, FRONTEN_PC.TXD, PARTICLE.TXD, grass\plant1.txd, GENERIC\VEHICLE.TXD (+ effectsPC.txd DAT-48) | существование | CTxdStore::LoadTxd(slot,file) 0x7320B0 `do { RwStreamOpen } while (!stream)`; вызовы CFont::Initialise 0x5BA690, CHud::Initialise 0x5BA850, LoadSplashes 0x5900B0, LoadAllTextures 0x572EC0, Init1 0x5BF8B7, CPlantMgr::Initialise 0x5DD95F, SetupCommonData 0x5B8F5E |
| UI-02 | Ошибка | файл не валидный TXD (нет rwID_TEXDICTIONARY / ошибка разбора) или отсутствует требуемое имя текстуры | разбор TXD; таблицы имён в отчёте §4.2 | LoadTxd 0x731DD0 возвращает false (все игнорируют); SetTexture 0x727270 → NULL; SetRenderState 0x727B30 ставит NULL-растр → плоские цветные квады; поиск без регистра, маски не ищутся |
| UI-03 | Ошибка | hud.txd без fist, siteM16, siterocket, radardisc, radarRingPlane, SkipIcon или любого из 62 radar_* + arrow (всего 68) | список без регистра | CHud::Initialise 0x5BA850 (0x8D128C), CRadar::LoadTextures 0x5827D0 (0x8D0720..0x8D0920) |
| UI-04 | Краш / Ошибка | fonts.dat отсутствует; [FONT_ID] не 0/1 (id 2 затирает CFont::m_Color..Sprite 0xC71A54..0xC71B26 — Sprite уже установлен на момент LoadFontValues 0x5BA6E5 → Краш при первой отрисовке); EOF внутри 26 [PROP]-строк (LoadLine NULL → sscanf(NULL)) → Краш; [PROP] < 26 строк или строка < 8 int → Ошибка (старые значения стека); нечисловые [UNPROP]/[REPLACEMENT_SPACE_CHAR] → Ошибка | секции; id 0 и 1 присутствуют, 26×8 PROP у каждого | CFont::LoadFontValues 0x7187C0; gFontData 0xC718B0 (2 × 0xD2) |
| UI-05 | Предупр. | radar00.txd..radar143.txd отсутствуют в IMG, или тайл без текстуры | 144 записи radar%02d.txd, ≥ 1 текстура (ваниль 128×128) | CRadar::Initialise 0x587FB0 (FindTxdSlot → −1), DrawRadarSection **0x586110** (−1 пропускается), GetFirstTexture 0x5861AF — первая текстура независимо от имени |
| UI-06 | Ошибка | loadscs без nvidia/eax/title_pc_US/loadsc0..14; fronten1 без arrow + 12 radio_*; fronten2 без back2..back8, map; fronten3 без back8_top, back8_right; fronten_pc без mouse, crosshair | имена по TXD | LoadSplashes 0x590180.., LoadAllTextures 0x572EC0 (таблицы 0x8CDF28/0x8CDF90/0x8CDFD0/0x8CDFE0) |

---

## 9. Аудио — audio/CONFIG, SFX, streams, surfaud.dat, vehicles/peds (AUD-*)

Важно: тестовая установка `D:\Grand Theft Auto San Andreas` **не стоковая по streams**: 10 стрим-паков нулевой длины (AA, CO, CR, DS, HC, MH, MR, RE, RG, TK), StrmPaks.dat перенаправляет станции на NJ/CH. Сток (Feb 11 2005): AA, ADVERTS, '', AMBIENCE, BEATS, CH, CO, CR, CUTSCENE, DS, HC, MH, MR, NJ, RE, RG, TK — 17 записей, №2 пустая и не используется TrakLkup. Калибровать AUD-09/26/27/28 надо на стоковых файлах (на этой установке AUD-27 даёт 255, AUD-28 — 1701 «ложных» находок).

Слоты, которые exe реально просит (все call-sites LoadSoundBank/LoadSound): 0..6 (init сущностей), 7..16 dummy-двигатели, 17 horn, 18 cop heli, 19 vehicle gen, 20..25 speech, 26..29 mission (0x1A+slot @0x4EC1F7/0x4EC20D), 30 footsteps, 31 doors, 32 swimming, 33..37 scanner, 40 player engine, 41 generic feet, 42 bullet pass. **38, 39, 43, 44 не запрашиваются.**

| id | Уровень | Условие | Как проверить | Движок |
|---|---|---|---|---|
| AUD-01 | Краш | BankSlot.dat отсутствует или ≤ 2 байт | размер > 2 | LoadBankSlotFile 0x4E0590 (0x4E05AC/0x4E05C1 return 0) → CAEMP3BankLoader::Initialise 0x4E08F0 → CAEAudioHardware::Initialise 0x4D9930 fail → CAudioEngine::Service 0x507791 → GetTrackPlayTime 0x4F1537 `mov ecx,[ecx+8]; mov eax,[ecx]` с m_pStreamingChannel NULL |
| AUD-02 | Краш | длина ≠ 2 + count×4820 (count = первый u16) | равенство | длиннее → Read(len−2) переполняет new(count×0x12D4) (0x4E05E9/0x4E05FD); **короче → (уточнено) сравнение 0x4E0608 всегда проходит (fread просит ровно len−2), хвост блока неинициализирован → мусорные размеры слотов → CMemoryMgr::Malloc(мусор) 0x4E065B** |
| AUD-03 | Краш | BankLkup.dat отсутствует/пуст | размер > 0 | LoadBankLookupFile 0x4DFBD0 (0x4DFBEC / 0x4DFC0C) → AUD-01 |
| AUD-04 | Краш | размер BankLkup % 12 ≠ 0 | остаток | count=len/12, new(count×12), Read(len) 0x4DFC33 → переполнение кучи на len%12 |
| AUD-05 | Краш | PakFiles.dat отсутствует/пуст | размер > 0 | LoadSFXPakLookupFile 0x4DFC70 |
| AUD-06 | Краш | размер PakFiles % 52 ≠ 0 | остаток | new(count×52), Read(len) 0x4DFCE4 |
| AUD-07 | Краш (уточнено: **или пуст**) | StrmPaks.dat отсутствует или 0 байт (count=0 проходит, но Initialise 0x4E0C76 делает lstrlen по Malloc(0) → мусорный/пустой путь → OpenFile fail → DVD fail → return 0 0x4E0D1A) | существование и размер > 0 | LoadStreamPackTable 0x4E0970 → Initialise 0x4E0C50 |
| AUD-08 | Краш | TrakLkup.dat отсутствует | существование | LoadTrackLookupTable 0x4E09F0 (0x4E0A17) |
| AUD-09 | Краш | первый стрим-пак (StrmPaks запись 0; **сток AA**) отсутствует | первые 16 байт StrmPaks до NUL; audio/streams/<name> существует (размер может быть 0) | Initialise 0x4E0C50: OpenFile 0x4E0CAB; DVD-fallback getDvdGamePath 0x747300; return 0 0x4E0D1A |
| AUD-10 | Ошибка | EventVol.dat отсутствует или < 45401 (0xB159) байт | размер ≥ 45401 | CAudioEngine::Initialise 0x5B9C60: OpenFile==0 или Read(0xB159)≠0xB159 → return до инициализации сущностей и CLoadingScreen::Continue; InitialiseCoreDataAfterRW 0x5BFA90 результат игнорируется WinMain (0x748C3F) — заодно пустые free-list у break/bone-node/IK-chain менеджеров; не краш (hardware уже поднят) |
| AUD-11 | Краш / Ошибка / Инфо (**уточнено**) | count слотов ≤ 42 и не в {38,39} → **Краш** (запрос слота == count проходит `cmp di,[esi+0xC]; jg` 0x4E06A5/0x4E07E4 → slotInfo = base+count×0x12D4 → Service пишет 0x12C0-байтовую таблицу за кучу); count ∈ {38,39} → Ошибка (слоты 40..42 молча выброшены: нет двигателя игрока / generic feet / bullet pass); count ∈ {43,44} → Инфо; ≥ 45 ок | count | LoadSoundBank 0x4E0670 / LoadSound 0x4E07A0 / Service 0x4DFE30; слоты > count отбрасываются 0x4E0790 |
| AUD-12 | Ошибка / Краш / Предупр. (**уточнено**) | записей BankLkup < 710 → **Ошибка** — все банки с id ≥ count немы (печатать, какие ванильные диапазоны умирают: напр. count 153 → «нет речи педов 365..709, нет mission-аудио 153..364»; реальная сборка Silent Hill с 153 записями работает); **Краш**, если count равен id банка, на который ссылается exe/данные: статические {0..6,13,27,28,29,30,31,39,44,51,52,59,60,74,82,105,128,138,143}, scanner 147..152, mission-таблица 0x8ABC70 (события 1800..1999: 0,20,31,34,35,37,70,100,153,158,160,163,167,171,183,198,209,222,226,242,265,266,291,292,293,296,298,304,310,319,339,345,351,352; 291 для 0xFFFF), player/dummy-банки строк 0x860AF0 для id из vehicles.ide, voice-банки 365..709 для голосов из peds.ide; script speech 147..364 не решается → Предупр. | size/12 vs множество ссылок | GetBankLookup 0x4E01C0 `<` → NULL при bankId == count → `mov cl,[eax]` 0x4E072E/0x4E0897; bankId > count отбрасывается 0x4E068D/0x4E07CD |
| AUD-13 | Ошибка | запись BankLkup с pakFileIndex ≥ count PakFiles | rec.pak < pakCount | Service 0x4DFE30: m_paStreamHandles[pak] 0x4E00E2 за блоком → мусорный CdStream handle → банк не грузится (тишина) |
| AUD-14 | Ошибка | запись PakFiles называет файл, отсутствующий в audio/SFX | имя до NUL; файл существует; сообщить диапазон банков пака | CdStreamOpen 0x4067B0 (тело за SecuROM-трамплином 0x1564A90; «0 при ошибке» — из gta-reversed) → банки читаются через handle 0 = слот 0 (FEET) → не те сэмплы/тишина |
| AUD-15 | Предупр. / Краш (**порог уточнён**) | запись PakFiles без NUL в 52 байтах → Предупр. (склеенное имя → AUD-14); NUL-свободный ран от начала записи ≥ **118** байт → Краш (сохранённые регистры), ≥ 134 → адрес возврата | NUL в 52; длина рана | strlen-цикл 0x4DFD71; rep movsb 0x4DFD94 в esp+0x14 (фрейм 0x84, префикс 10 байт) |
| AUD-16 | Ошибка | offset + 0x12C4 + size > размер audio/SFX/<pak> | сравнить с длиной пака | CdStreamRead ((size+0x12C4)>>11)+2 секторов за EOF → старые данные буфера / запрос не завершается |
| AUD-17 | Предупр. / Ошибка (**понижено**) | numSounds (i16 по BankLkup.offset) > 400 → Предупр. (ломается только звук 399: следующий offset заворачивается через %400 0x4E030B → отрицательная длина); numSounds < 0 → Ошибка (каждый GetSoundBuffer 0x4E0280 возвращает 0 → немой банк); 0 легален (ваниль SPC_GA 435,504,507,512,513,523,555,580,585,588,593) | диапазон | LoadSound отбрасывает soundId ≥ 400 (0x4E07B4), в exe id зашиты < 400, по numSounds никто не итерирует — «мусорный указатель» невозможен |
| AUD-18 | Краш / Ошибка (**уточнено**) | смещения первых numSounds записей не неубывающие или последнее > size (условие `[0]==0` **выброшено** — ненулевое первое смещение просто пропускает байты); банк, грузимый одиночным звуком (speech 144..146, 365..709, scanner 147..152, mission 147..364) → **Краш** (soundSize заворачивается в huge uint 0x4E0094 → sectors (huge>>11)+2 0x4E00AD → Malloc fail / копия state 3 огромной длины); банк целиком → Ошибка (отрицательная длина каналу, большой копии нет) | таблица заголовка | Service 0x4DFE30 state 2; GetSoundBuffer 0x4E0300..0x4E0330 |
| AUD-19 | Краш / Предупр. (**поднято**) | в банке меньше звуков, чем ванильный numSounds для этого id (Приложение A отчёта) **и** BankLkup.size > размер целевого слота (speech 83456 / PLY 98304 — практически всегда для voice-банков) → **Краш**; влезает в слот → Предупр. | numSounds(bank) ≥ vanilla[bank]; сравнить size со слотом | заголовок всегда читается 4 секторами (8192 ≥ 0x12C4), неиспользуемые записи в ванили нули → LoadSound с soundId ≥ numSounds даёт soundSize = size − 0 = **весь банк** (state-2 else 0x4E007F..0x4E0096) → копия целиком в speech-слот; ненулевой мусор в хвосте таблицы → отрицательный размер → Malloc(huge) |
| AUD-20 | Краш | банк, грузимый целиком, больше своего слота: пары 59→0, 60→1, 39→2, 27→3, 52→4, 143→5, 105→6, 74→17, 13→18, 138→19, 51→31, 128→32, FEET0→41, FEET1..6→30, 28/29/**30/31**→42 (bullet pass циклит 28..31, 0x4DDEDB/0x4DDEE5), 44→40; dummy-банки машин (0x860AF0 строка +4) → min(size[7..16]); player-банки (+2) → size[40]; **mission-банки** (таблица 0x8ABC70, PreloadMissionAudio 0x4EC190 → GetBankAndSoundFromScriptSlotAudioEvent 0x4D9CC0 → LoadSoundBank(bank, 0x1A+slot)) → ≤ size[29] (253630) Краш-класс, > size[26] (148456) Предупр. (влезает только в слот 29; ваниль 35: 249980, 339: 248826, 209: 211024) | BankLkup.size ≤ BankSlot.size[slot] по парам (ваниль: dummy ≤ 63952 ≤ 74496; player ≤ 253518 ≤ 290048; 30/31 → 73484/74866 ≤ 86272) | Service 0x4DFE30 state 2 rep movsd BankLkup.size байт в m_pBuffer+slot.offset без сравнения со slot.size (нигде в 0x4DFE30..0x4E01AF) |
| AUD-21 | Краш | одиночный speech-звук больше слота: банки 144..146 и 365..689 soundSize > min(size[20..24]) = 83456; PLY 690..709 > size[25] = 98304; **scanner 147..152**: любой звук > 30178 → Краш, > ванильного максимума банка (147:30134, 148:10080, 149:8992, 150:15906, 151:12866, 152:16578; слоты 33..37 = 16028/12910/9114/10124/30178) → Предупр. (пара банк→слот строится динамически в AddAudioEvent 0x4E71E0); **loading tune**: банк 82 звуки ≤ min(size[2], size[5]) = 1048576 (ваниль макс 554344) | soundSize = table[i+1].offset − table[i].offset (последний: size − offset); ваниль макс 70938 / 85468 | Service state 3 копия без проверки slot.size |
| AUD-22 | Ошибка / Предупр. | script speech (банки 147..364): звук > size[29] (253630) → Краш-класс; > size[26] (148456) → Предупр. | по заголовку банка как AUD-21 (ваниль макс 210200) | слоты 26..29 выбираются скриптом в рантайме |
| AUD-23 | Инфо / Ошибка | сумма размеров слотов ≠ 8578144 → Инфо; слот размера 0 → Ошибка | сумма; нули | CalculateBankSlotsInfosOffsets 0x4DFBA0; m_nBufferSize = sum; Malloc(sum) |
| AUD-24 | Ошибка | записей TrakLkup < 1922 (size % 12 ≠ 0 → Предупр.) | size/12 ≥ 1922 | GetTrackInfo 0x4E0A70 / GetDataStream 0x4E0D20: id > count → NULL (0x4E0A91 ja / 0x4E0D41 jae; id == count — чтение за таблицей); ReadCallback 0x502584 → 0 → ov_open fail → декодер удалён (CAEStreamThread::Service 0x4F13A6..) → немой трек |
| AUD-25 | Ошибка | запись TrakLkup с streamPakIndex ≥ count StrmPaks | rec.pak < pakCount | GetTrackInfo 0x4E0AA3 jbe (off by one) → CAEDataStream::Initialise 0x4DC2B0 мусорное имя → CreateFileA fail 0x4DC2DA |
| AUD-26 | Ошибка | стрим-пак, на который ссылается ≥ 1 трек, отсутствует (запись 0 → AUD-09) | только используемые записи StrmPaks (сток: №2 пустая и не используется) | CAEDataStream::Initialise: INVALID_HANDLE → false → декодер выброшен |
| AUD-27 | Ошибка | offset + 0x1F84 + length > размер audio/streams/<pak> (length == 0 → GetFileSize, весь файл) | сравнить с размером пака | SetFilePointer за EOF, ReadFile 0 → короткий заголовок/Ogg; FillBuffer-клэмп 0x4DC1C3.. реально не ограничивает |
| AUD-28 | Ошибка (**ключ исправлен верификацией**) | 4 расшифрованных байта по offset+0x1F84 ≠ `OggS` | byte[i] ^= key[(p+i)&15], key = **EA 3A C4 A1 9A A8 14 F3 48 B0 D7 23 9D E8 FF F1** (ключ отчёта …5D E8 96 A2 неверен в байтах 12..15 — с ним 1701 из 1922 ванильных треков «не проходят») | GetDataStream 0x4E0E1A → CAEVorbisDecoder::Initialise 0x5024D0; цикл расшифровки 0x4F17D0 |
| AUD-29 | Предупр. | запись StrmPaks без NUL в 16 байтах | NUL в 16 | Initialise 0x4E0C76 lstrlen — имя склеивается со следующим, буфер по strlen (нет переполнения) → AUD-26 |
| AUD-30 | Краш | surfaud.dat отсутствует (= PDD-55) | существование | LoadSurfaceAudioInfos 0x55F2B0 (= DAT-03) |
| AUD-31 | Предупр. | первый токен не из 179 имён (с регистром) | точно DEFAULT..RAILTRACK | GetSurfaceIdFromName 0x55D220 → 0 (0x55E5AB..0x55E5B1) → флаги на DEFAULT |
| AUD-32 | Ошибка | < 10 токенов или нецелый флаг | 10 токенов, 2..10 int | sscanf: незаполненные int = мусор стека → случайные биты 10..18 SurfaceInfo[id] |
| AUD-33 | Краш | первый токен ≥ 64 (= PDD-62) | длина ≤ 63 | %s в local_40[64] |
| AUD-34 | Инфо | поверхность дважды / отсутствует | счёт по поверхности == 1 | последняя побеждает; отсутствующая — нулевые флаги |
| AUD-35 | Ошибка / Предупр. | cars id > 630 → Ошибка (36 байт копируются из 0x860AF0+(id−400)×36 без проверки; после 231 строки — таблица байтов 0/1 → мусорный тип/банки/радио); 612..630 → Предупр. (soundType 10 = немая; **611 utiltr1 и 610 тоже тип 10**, замена на этих id — немая) | id в 400..630 | CAEVehicleAudioEntity::Initialise 0x4F7670; типы {0..5,8,9,10}, макс player-банк 142, dummy 141 |
| AUD-36 | Инфо | id 400..611 занят машиной другого класса, чем ванильная | печатать ванильную строку (engine bank, horn, door, radio, siren) | настройки берутся по id |
| AUD-37 | Инфо | voice-имена peds.ide — все банки таблиц 0x8AF700/0x8BA470/0x8BBED0/0x8BE5B8/0x8C4288/0x8C64AC < 710 | ничего сверх AUD-12 | |

---

## 10. Открытые вопросы (не решены декомпиляцией — оставлены на уровне выше/ниже, как указано)

* CTrain (0x6F6030), CBoat (0x6F2940), f_plane: какие фреймы разыменовываются без проверки — не дизассемблировано (VEH-21 остаётся Предупреждением).
* PDD-17 (не-пед в pedgrp.dat → Краш) — на уровне CPopulation::AddPed по именам gta-reversed, инструкционно не прослежено; подтвердить хуком или держать Ошибку.
* PDD-28: порядок блоков зон popcycle известен только по ванильным комментариям.
* CLO-03/04, CUT-07: что именно после RequestFile с мусорным offset/size — зависание в цикле повтора CdStream или NULL-кламп; нужен хук на 0x5A55A0.
* CUT-07: всегда ли фатально переполнение 20 special-моделей (ids 320+) или только когда затёртая IDE-модель используется.
* IMG-13: реакция стрим-потока на короткий CdStreamRead (RetryLoadFile / CD error) не прослежена.
* IMG-01: точное место краша, когда player.img получает id 0 — выведено, не дизассемблировано.
* SCM: потребитель seg3 «largest streamed script size» (CStreamedScripts+0xA40) не найден.
* GXT: есть ли у CMessages/CFont фиксированный буфер меньше самой длинной ванильной строки (521) — не проверено.
* AUD-11: просит ли exe слот 44 (SND_BANK_SLOT_EFFECT2) — не найдено ни одного вызова; порог оставлен по фактическому множеству слотов.
* AUD: диапазоны sampleRate (ваниль 2021..44100) / headroom (−200..6229) в заголовках банков — пути к крашу не найдено, правило не выдано.
* HND-06: читатель frontLights/rearLights (+0xDC/+0xDD) не найден — Инфо до появления потребителя.
* VEH-12: 16384 для PC_Scratch — из gta-reversed; в exe только «bss, следующий символ gamma @0xC92134 (+0x406C)».
