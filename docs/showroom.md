# Showroom（検証専用シーン）

敵の死亡時は、残っている全部位が面の破片になって外側へ飛び散り、重力で落下します。死亡演出はBreak Modeの設定によらずFace方式を使用します。Local HP・Shared HPによる死亡とBomberの起爆は通常ゲームと共通の死亡処理を使用します。Sharedのみ／破壊不可の部位も死亡時には崩れ、すでに飛んだ部位は再生成しません。Freeze Enemies中でも死亡演出は動き、F5のリセットで全身が復元されます。

Debug／Developmentのタイトルで **F3** または **Showroom F3** を押します。本編では **FPS Controls → Open Showroom** からも入れます。Showroomは独立したシーンとして登録し、射撃・武器・敵の実装はGameSceneと共用しています。ReleaseではShowroomを登録せず、移動ボタンや専用ImGuiも表示しません。本編の起動先はGameのままです。

## 配置

専用の48m×56mの床と外壁を使用します。プレイヤー開始位置から見て左前方に武器2列、右前方に敵5体。奥にも自由に歩ける空間があります。敵の手前に5m・10m・15mの射距離を示す細い床の帯があります。

武器：pistol / smg / rifle / shotgun / pump_shotgun / auto_shotgun / burst_rifle。近づいてEで何度でも装備できます。展示は消えません。既存の **Weapon System → Equip Weapon (Debug)** とShowroom一覧の **Equip** ボタンからも即座に切り替えられます。再装備すると弾数・Reload・Burst・Cooldownは初期化されます。

敵は左からNormal / Ranged / Fast / Tank / Bomber。中心間隔4m、足元Y=0、初期向きはプレイヤー側です。EnemySpawnの配置データを直接読み、入場時に全員を生成します。ShowroomではTriggerの更新・Goal判定を実行しません。

別個体の **Normal Test**（ID: `normal_test`）を左奥の **(-14, 0, 24)** に配置しています。`resources/enemy/boss/normal.test.enemy.json` を使うNormal型で、既存5体の列から離してあります。

武器エディター左の **Normal Test (.enemy.json)** で `resources/enemy` 以下の部位アセットを選べます。試射中は **Tab** で同じ欄を開閉できます。欄を開いている間は試射を一時停止し、マウスを解放します。Blenderで同じファイルへ上書きした場合は **Reload JSON** を押します。切り替えや再読込の成功時にショウルームをリセットし、選んだ部位アセットは入場中のNormal Testに適用します。

現在のWeapon Editor画面には **Reset Showroom (F5)** と **Freeze Enemies** を表示します。試射中も **F5** でリセットできます。敵を元の位置・HP・部位で再生成し、プレイヤーHP・現在の武器の弾薬・Pickupを回復、飛翔中の弾と射撃／命中記録を消去します。現在位置、AI静止設定、未保存の武器編集内容は維持します。敵データは入場時に読み込んだ内容で復元します。

## 操作

- **Freeze Enemies**：初期ON。AI移動・攻撃・敵同士の押し合いを止めます。射撃によるDamageState変化・部位破壊・破片の動きは継続します。既に発射済みの敵弾／爆弾も継続します。
- **Enable Enemy AI**：通常AIを動かします。Ranged／Bomberの飛翔物を確認できます。全体を完全停止する場合はF1を使います。
- **Reset Enemies**：元の配置・HP・部位へ再生成し、敵弾／爆弾を消去、Player HPも回復します。AIのFreeze設定は維持します。履歴はリセット時点から記録し直します。
- **Reset Pickups**：展示Pickupを復活させます。通常は拾った直後に復活するため、再配置操作は不要です。
- **Restart Scene**：Showroomを再読み込みし、AI静止から開始します。
- **Teleport to Start / Weapon Area / Enemy Area / Firing Line (15m)**：確認位置へ移動します。
- **Return to Game**：本編へ戻ります。本編は通常の初期状態から開始します。

F1ポーズ、`,`／`.`の履歴操作、JSON編集も使用できます。JSON FileのStageはShowroom内ではshowroom.jsonを指し、保存後もShowroomへ戻ります。敵／武器定義の変更は本編と共通です。

## 確認表示

**Show Enemy Labels / Markers / Collision / Part Hit Boxes / Stage Colliders / Weapon Pickup Labels**で表示を切り替えます。ラベルはScene画像上のDebug表示で、当たり判定はありません。Markerも射撃対象に追加していません。

Showroom一覧はEnemy ID、Type、Visual Scale、Collision Radius／Heightと、生存状態を表示します。Head Yは実際の頭部パーツ中心をワールド変換した高さ、Head minus eyeはプレイヤー視点との差です。既存のEye heightとJSONのvisualScaleを調整するときの比較に使えます。今回、共通の敵サイズやプレイヤーの目の高さは変更していません。

Current Weapon・Ammo・Reload State・Burst残弾・ADS FOV／Spreadは既存Weapon Systemウィンドウを使用します。

## Blenderで編集

- `resources/levels/showroom/showroom.blend`
- `resources/levels/showroom/showroom.gltf` / `.bin`
- `resources/levels/showroom/showroom.json`

既存のYanEngine Level Exporterを有効にして.blendを開き、EnemySpawns／WeaponSpawns／PlayerSpawn／Geometryを編集してExportします。Enemyの各Poolは1タイプ、Weaponの各Poolは1武器を指定してください。敵は名前の番号順で配置・一覧化します。Trigger／Goalは不要です。新しい武器定義を追加した場合はWeaponSpawnも追加してください。

`tools/blender/create_showroom.py`は初期シーン生成用です。再実行すると手作業の配置を初期配置で上書きするため、通常の編集は.blendからExportしてください。

## 検証

`tools/test-showroom.ps1`：モデル依存ファイル、全武器の網羅、繰り返しPickup、5体の順序／間隔／向き、Trigger／Goalなし、部位ダメージ、壁・床の衝突、本編Pickupが消費される従来動作を検証します。

`tools/test-blender-showroom.ps1`で.blendとExport済みJSONの一致、素材設定、初期視野への配置範囲も確認します。

Debug／Release x64は警告0・エラー0。新規Showroomテスト、既存C++テスト9本、既存Blenderテストは成功しています。

ゲーム画面での実操作確認は実施していません。次を手元で確認できます。

- [ ] 初期状態で5体が静止し、Normalの標準サイズ・Markerなし、Rangedの青、Fastの小さい体格と黄色、Tankの大きい体格と紫、Bomberのオレンジを比較できる。
- [ ] 各武器をEまたはDebugで装備し、Ammo／Reload／ADS／Burst／Shotgunを比較できる。
- [ ] 白→赤のDamageState、各部位への命中、部位破壊・死亡を確認でき、Markerは射撃を遮らない。
- [ ] Reset Enemiesで破壊された部位も復活し、再試行できる。
- [ ] AIを有効にすると敵が動き、Ranged／Bomberが攻撃する。
- [ ] 頭部と視点の高さを比較し、Collision／Hit Boxを必要に応じて表示できる。
- [ ] 本編へ戻るとStage01の通常スポーン・Goal進行が動く。

## 敵サイズのリアルタイム調整

F1またはESCでマウスを出し、**FPS Controls → Selected Enemy**で対象を選びます。**Visual Scale (Uniform)**はXYZを同じ値へ、**Visual Scale XYZ**は各軸を個別に変更します。ポーズ中も次の描画フレームで本体・Marker・射撃用部位判定へ反映します。HP・破壊状態・AIはリセットしません。既に飛び散った破片のサイズは維持します。

**Collision Radius / Collision Height**もその場で調整できます。Visual Scaleとは独立です。対象は選択中の1体だけで、Restart／Reset後はJSONの値に戻ります。永続化には下記のSave Dimensions to JSONを使用できます。これらの編集欄はDebug専用です。

### サイズの保存・読み込み

FPS Controlsで敵を選択し、**Save Dimensions to JSON**で現在のVisual Scale・Collision Radius・Collision Heightをenemies.jsonの同じDefinition IDへ保存できます。他タイプや攻撃力などは変更しません。検証後に保存し、直前のファイルは`.debug-backup`へ保管します。成功・失敗はボタン下に表示します。

**Load Dimensions from JSON**はディスク上の3設定を選択中の1体へ即時反映します。HP・AI・部位破壊は維持します。保存した値を全個体や今後のスポーンへ反映するにはRestart Sceneを使用してください。Reset Enemiesはシーン開始時に読み込んだ定義で再生成するため、ディスクからの再読込にはRestartを使用します。
