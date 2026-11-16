# 武器定義・Pickup・射撃方式・Reload

性能は `resources/Data/weapons.json`、配置と候補は `resources/levels/fps_spawns.json` で設定します。変更はRestart Stageで再読込します。専用の銃モデルは追加せず、同梱Cubeを武器ごとのサイズ・色で表示しています。モデル、定義、Pickup描画リソースはStage初期化時に準備し、Pickup時や射撃時のモデルロードはありません。

武器に2.2以内まで近づき、Eで最寄りの1個を取得します。同距離ならJSONの順で選びます。ステージに配置された武器は繰り返し取得できます。別の武器に持ち替えると、旧武器をその場に落とします。同じ種類のドロップがすでに地面にある場合は、その位置・向き・残弾を今回落とした武器の情報で更新します。装備中と同じ武器を拾うと、現在の武器を装備したまま、装填数と予備弾数を定義の初期値に戻します。地面に落とした武器を拾うと、その拾得物は回収されます。

| ID | FireMode | Ammo/shot | Pellet | Magazine / Reserve | Reload |
|---|---|---:|---:|---|---|
| pistol | SemiAuto | 1 | 1 | 12 / 60 | Magazine |
| smg | FullAuto | 1 | 1 | 30 / 120 | Magazine |
| rifle | FullAuto | 1 | 1 | 30 / 90 | Magazine |
| shotgun（既存互換） | SemiAuto | 1 | 8 | 8 / 32 | Magazine |
| pump_shotgun | SemiAuto | 1 | 8 | 8 / 40 | PerRound |
| auto_shotgun | FullAuto | 1 | 6 | 20 / 80 | Magazine |
| burst_rifle | Burst（3発） | 1 | 1 | 30 / 90 | Magazine |

武器IDによる射撃・装填の特殊分岐はありません。既存shotgunの挙動は維持し、別定義としてpump_shotgunとauto_shotgunを追加しています。

## 性能フィールド

- `fireMode`: SemiAuto / FullAuto / Burst。省略時SemiAuto。
- `fireInterval`: 通常の発射間隔。Burstでは最終発射後から次Burstまでの待ち時間。DebugのRPMは60/fireInterval（Burst内部のレートではありません）。
- `burstCount`, `burstInterval`: 既定3発 / 0.07秒。クリック1回で開始し、マウスを離しても予約分は発射します。重複予約は不可。残弾不足なら打ち切ります。0秒間隔も使用可能です。
- `ammoPerShot`: 1回の発射で消費する弾薬。既定1。これ未満の残弾では発射できません。
- `pelletCount`: 発射1回で生成するRay数。既定1。Rayの本数で弾薬消費は変わりません。
- `damage`: Rayごとのダメージ。Shotgunの全Pelletが当たれば複数回加算されます。Shot Countは発射回数、Hit Countは命中Ray数です。
- `range`, `magazineSize`, `reserveAmmo`, `maxReserveAmmo`: 武器ごとの射程、容量、取得時Reserve、Reserve上限。
- `hipSpreadDegrees`, `adsSpreadDegrees`: Hip/ADSの拡散半角。既存のADS Blendで補間します。旧 `spreadDegrees` もHip値として読めます。
- `adsFov`, `adsTransitionTime`, `adsSensitivityMultiplier`: 既存のFOV・感度設定を維持。今回ADS補間処理は変更していません（既存のtarget一致時の修正を保持）。

Burstの同一フレーム内の期限を過ぎた弾も漏らさず発射し、実際の最終発射時刻からCooldownを計算します。FullAutoは既存同様フレームごとの入力評価です。ESC・フォーカス喪失・Scene操作再開クリックの抑制時は進行中Burstを取り消し、裏で発射しません。

## Reload

`reloadMode` はMagazine（既定）またはPerRound。

MagazineはRでStartingへ入り、`reloadTime` 後に不足分をReserveから一括移動してNoneへ戻ります。射撃による中断はできません。

PerRoundはRから `reloadStartTime` 待ち、最初の1発を装填します。続けて `reloadPerRoundTime` ごとに1発を装填し、満タンまたはReserve=0でFinishingへ移ります。`reloadEndTime` 後にNoneになります。既定は0.25 / 0.55 / 0.30秒。各時間は0も許可します。

`reloadCanInterrupt=true`（既定）のPerRoundは、ammoPerShot以上の残弾がありCooldownも終了していれば、射撃入力で中断して発射できます。装填済みの弾は残り、未完了の装填は取り消します。空Magazineなら装填前の入力で中断しません。Starting / InsertingRound / Finishingのいずれでも同じ条件です。falseなら終了まで射撃不可です。

Reload残り時間のDebug値は現在のフェーズの残り時間です。HUDはどのフェーズでもRELOADINGを表示します。RによるReload開始は残りBurstをキャンセルします。

## 配置と再抽選

- WeaponSpawn_01: (3,0.5,-2)、Pistol / SMG
- WeaponSpawn_02: (3,0.5,20)、SMG / Rifle / 既存Shotgun / Pump / Burst Rifle
- WeaponSpawn_03: (3,0.5,44)、Rifle / 既存Shotgun / Pump / Auto Shotgun / Burst Rifle

`weaponPool` はID文字列配列または `{ "id": "...", "weight": 1 }` の配列。抽選はStage初期化時だけです。weight=0は抽選対象外、全候補0はエラーです。

`weaponRandom: { "useFixedSeed": true, "seed": 12345 }` で同じ設定・同じ実行環境のRestart配置を再現できます。DebugのUse Fixed Seed / Seedは次のRestartから適用します。候補や順番を変えると同じSeedでも結果は変わります。非固定Seedでも偶然同じ武器が選ばれる場合があります。

StageClear後はPickup・射撃・Reload・Burstを含めWeaponRuntimeの更新自体を停止します。

## JSON検証

ammoPerShot、pelletCount、burstCount、magazineSizeは正整数。Burst間隔と各Reload時間は有限の0以上です。未知のFireMode/ReloadModeや不正値はLoad失敗となり、直前の正常な定義・配置・抽選結果を維持します。旧JSONで新しいフィールドがない場合は既定値を使用します。

## 手元で確認する項目（今回は画面操作未実施）

武器を確実に試すには、ESCでカーソルを解放し、Weapon Systemの **Equip Weapon (Debug)** から選択してください。JSONに定義された全武器を選べます。弾数・Reload・Burst・Cooldownは初期化され、配置済みPickupは消費しません。同じ武器の選び直しでも初期化できます。クリア後は変更できないためRestart Stageを使用してください。Restart時の初期武器はJSONの `initialWeapon` に従います。Pickupも試す場合は配置のweaponPoolを対象ID1つにします。

1. **SemiAuto**：Pistol/PumpでLMB長押ししても1発だけ。クリックを繰り返すとfireInterval以上の間隔で発射。
2. **FullAuto**：SMG/Auto Shotgunで長押し連射。離すと停止。Sceneを操作し直すためのクリックでは発射しない。
3. **Burst Rifle**：1クリックで3発、0.07秒間隔。その後0.45秒待ち。連打してもBurstが重ならない。残弾1～2発ではその分だけ発射して終了。
4. **Pump Reload**：数発撃ってR。0.25秒で最初の1発、以後0.55秒ごとに1発ずつMagazine増加/Reserve減少。満タン後0.30秒でRELOADING消失。
5. **Reload途中射撃**：Pump装填途中にLMBで中断して射撃。空のときは装填前に射撃しない。Magazine方式は中断不可。
6. **AmmoとPellet**：Pumpの1回射撃で弾数は1だけ減る。ammoPerShot=2に変えてRestartすると2減るが、Pellet数は8のまま。
7. **Pickup/Restart**：Eで最寄りの武器を拾う。別の武器に持ち替えた場合は、落とした武器を再取得して残弾を確認する。同じ武器を拾った場合は、装填数と予備弾数が初期値に戻り、落とした武器が増えないことを確認する。固定Seedで再現し、Restartで取得状態と初期装備がリセットされる。
8. **ADS/部位/クリア**：RMB長押しで視点が揺れずFOV・感度・Spreadが変わる。各武器で部位HPとFace/Chunk破壊が動く。Goal後は弾数・Reload/Burstが進まない。

自動検証は `tools/test-weapons.ps1`。武器関連に加え、Stage/Spawn/Enemy部位・AI・破片/FPS/Raycastの既存テストを実行します。描画・操作感は自動テストの検証範囲に含めません。


## 武器枠・種類・レアリティとスポーン条件

武器定義に次の独立したメタデータを設定できます。射撃・リロード・ダメージ計算には影響しません。

| フィールド | 値 | 省略時 |
|---|---|---|
| `slot` | `Main` または `Sub` | `Main` |
| `type` | `AssaultRifle` / `SMG` / `Shotgun` / `Pistol` / `Revolver` / `Melee` などの空でない文字列 | `Unknown` |
| `rarity` | ★1～★5に対応する整数1～5 | 1 |
| `weight` | 全武器から抽選する場合の重み（0～1e9） | 1 |

`slot` は種類とは別に指定します。TypeからSlotを推測したり、レアリティに応じて性能を自動補正したりしません。Typeを追加するだけならC++の変更は不要です（大文字小文字を区別します）。`Melee`という分類の指定だけでは近接攻撃処理は追加されません。Main/Subの2本同時所持や持ち替えは今回の分類追加に含まず、Eで現在の装備を交換する仕様を維持しています。

同梱データはPistol＝Sub ★1、SMG＝Main ★2、AssaultRifle＝Main ★3、Shotgun＝Main ★2、Pump Shotgun＝Main ★3、Auto Shotgun＝Main ★4、Burst Rifle＝Main ★3です。各値はJSONで変更できます。

武器定義への追加例（既存の性能フィールドと併記）：

```json
{ "type": "Pistol", "slot": "Sub", "rarity": 1, "weight": 1 }
```

スポーン地点は `filter` で制約します。序盤のSub武器を全武器から抽選する例：

```json
{
  "id": "EarlySub",
  "position": [3, 0.5, -2],
  "filter": { "slot": "Sub", "minRarity": 1, "maxRarity": 2 }
}
```

- `filter` 省略：全枠・★1～★5・全Type。
- `slot` 省略：Main/Subの両方。指定時はその枠だけ。
- `minRarity` / `maxRarity` 省略：それぞれ1 / 5。両端を含みます。
- `types` 省略または空配列：全Type。`["Shotgun"]` ならShotgun限定。複数指定はOR条件。
- 枠・レアリティ・Typeの条件同士はANDで結合します。

Main限定は `{"slot":"Main"}`、★3以上は `{"minRarity":3}`、★1～★2は `{"minRarity":1,"maxRarity":2}`、Shotgun限定は `{"types":["Shotgun"]}` と指定します。

既存の重み付き候補と組み合わせる例：

```json
{
  "id": "MainRare",
  "position": [3, 0.5, 20],
  "filter": { "slot": "Main", "minRarity": 3 },
  "weaponPool": [
    { "id": "pistol", "weight": 10 },
    { "id": "rifle", "weight": 3 },
    { "id": "pump_shotgun", "weight": 1 }
  ]
}
```

この例ではPistolを除外し、RifleとPump Shotgunを3:1で抽選します。

`weaponPool` があればその候補・重みを使います（文字列IDや重み省略は従来どおり1）。武器定義のweightと乗算しません。`weaponPool` を省略した場合だけ全武器と定義側weightを使います。指定した空poolは従来どおりエラーです。絞り込み後の候補に対して1回だけ重み付き抽選を行い、weight=0は選びません。条件なしの既存poolは並び順と重みを保ち、同じSeedで同じ結果になります。

候補なし・一致候補の重みがすべて0・範囲逆転・不正Slot・範囲外/小数のレアリティ・filter内の未知フィールドはエラーになります。スポーンIDを含むエラーを返し、読み込み済みの有効な状態は保持します。無関係な候補で代替しません。DebugのWeapon Systemで装備の分類、地点の条件、絞り込み後の候補を確認できます。

設定対象は使用するレベルJSONの `weaponSpawnPoints` です。既存ステージの候補設定は変更していません。BlenderエクスポーターのUIには新条件を追加していないため、エクスポート後にJSONで設定してください（再エクスポート時には再適用が必要です）。
