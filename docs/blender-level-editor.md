# Blenderでステージを作る

YanEngine Levelアドオンを使い、Blenderの配置から見た目のglTFとゲーム設定のJSONをまとめて出力できます。本番ゲームUIは追加していません。

## まず付属サンプルを開く

編集用ファイルは `resources/levels/stage01/stage01.blend`、アドオンは `tools/blender/yanengine_level_exporter.py` です。Blender 4.4以降を対象にしており、今回の自動検証環境はBlender 5.0.1です。

### 1. Add-onを導入する

1. Blenderを起動します。
2. **Edit → Preferences → Add-ons** を開きます。
3. 右上のメニューから **Install from Disk**（日本語表示ではディスクからインストール）を選びます。
4. `tools/blender/yanengine_level_exporter.py` を選び、インストールします。
5. **YanEngine Level** を有効にします。
6. `stage01.blend` を開きます。3D Viewportにマウスを置いて **N** キーを押し、右側の **YanEngine Level** タブを開きます。

Object Typeなどの設定は.blendへ保存されます。変更後は **Ctrl+S** で保存してください。Exportは.blend自体の保存を代行しません。

### 2. Stage IDとOutput Directoryを設定する

パネル上部はScene全体の設定です。

|項目|Stage01の設定|
|---|---|
|Stage ID|`stage01`|
|Project Root|`CG2_Setup.sln` と `resources` があるフォルダー|
|Output Directory|`resources/levels/stage01`|
|Fixed Seed|抽選を再現したい場合にON|

付属.blendのProject Rootは `//../../../` です。.blendから見た相対パスなので、プロジェクトごと別のPCへ移動できます。.blendだけ別の場所へ移す場合はProject Rootを実際のプロジェクトフォルダーに直してください。

出力先の末尾フォルダー名はStage IDと同じにします。Project Root設定時は `Project Root/resources/levels/Stage ID` へ出力します。Enemy / Weapon Spawnがある場合は、定義の読み込みと検証のためProject Rootが必須です。定義が読めない場合も保存済みIDは変更しません。

現在のGameSceneが起動時に読むのは `resources/levels/stage01/stage01.json` です。まずはStage IDをstage01のままで編集してください。別IDへ出したステージを起動するにはGameSceneのlevelPathを変更します。

### 3. FloorやWallをStatic Meshにする

1. **Shift+A → Mesh → Cube** などで形を作ります。
2. **G**で移動、**R**で回転、**S**で大きさを変更します。
3. 対象を選択し、パネルの **YanEngine Object Type** を **Static Mesh** にします。
4. Floor、Wallなど分かる名前を付けます。

Static MeshだけがglTFの見た目へ出力されます。Modifierを評価したメッシュと親を含む配置を一時メッシュへ焼き込みます。元のオブジェクト・選択状態・Sceneは保持します。Blender単位1をゲーム単位1として扱います。

### 4. Collisionを設定する

|Collision|動作|
|---|---|
|None|描画のみ。壁として射撃や移動を止めません。|
|Box|そのStatic MeshのローカルBounding Boxを使います。|
|Custom|別オブジェクトをCollider Object欄に指定します。|

複雑な岩や建物は、見た目と単純な当たり判定を分けられます。

1. Cubeを追加し、`COL_Building` などの名前にします。
2. Object Typeを **Collider** にします。Meshの場合はワイヤー表示になります。
3. 建物の当たり判定にしたい形へ移動・回転・拡縮します。
4. 建物側のStatic Meshを選び、CollisionをCustom、Collider ObjectをそのCubeにします。

Colliderはゲームでは見えません。Colliderを単独で配置することもでき、全Colliderが有効になります。Static Mesh側をNoneにして独立したColliderを置いても構いません。Customは二重の判定を作らず、参照したColliderを使用します。

Colliderは回転できます。ローカルAABBとWorld Transformを保持するので、斜めの壁にも対応します。サイズ0・負Scale・親の非均一Scaleによるシアーは検証エラーです。必要ならObject → Applyで変換を適用し、形を確定してください。

床の上面はPlayer Spawnの足元の高さへ合わせてください。現段階は水平移動を前提とし、階段・斜面歩行・落下・ジャンプは追加していません。マップ外へ出ないよう、外周には壁Colliderを置きます。

### 5. Player Spawnを置く

1. **Shift+A → Empty → Arrows** を追加します。
2. Object Typeを **Player Spawn** にします。
3. プレイヤーの足元を開始させたい場所に置きます。
4. 向きを変更したい場合はBlenderのZ軸回転を使います。

Player SpawnはScene内に必ず1つ置いてください。0個・複数個はExportエラーです。Blenderの **-Y方向がゲームの前方+Z**、**Zが高さ**です。プレイヤーの無回転時の視線はBlender -Y方向になります。Player Spawnを壁の中へ置かないでください。

ゲーム開始とRestart Stageで、この位置・回転を適用します。カメラは既存の目線高さを足した位置になります。

### 6. Enemy Spawnを置く

1. Emptyを追加し、Object Typeを **Enemy Spawn** にします。
2. IDを設定します。空欄ならObject名を使用します。
3. **Spawn Group** に `A` などを入力します。
4. 移動・回転でスポーン場所を設定します。

Enemy Spawnそのものはゲームに表示されません。IDは他の有効オブジェクトと重複させないでください。敵の中心が壁に重ならない位置を選びます。

### 7. Enemy Poolを設定する

**Add Entry** を押すとIDとWeightの行が増えます。右のマイナスボタンで行を削除できます。

例：normal=5、fast=2、ranged=2、tank=1、bomber=1。

IDはProject Rootの `resources/Data/enemies.json` から選択します。メニューには `Normal (normal)` のように表示名とIDが表示されます。Weightは0以上、合計は0より大きい値にします。空Poolはエラーです。確実に1種類だけ出すには、そのID・Weight=1の行だけを残します。

JSONの変更はパネル再描画時に再読み込みされます。すぐ反映したい場合は **Refresh Definitions** を押してください。候補はJSONから生成するため、アドオンのコード変更は不要です。既存.blendのIDはそのまま保持します。削除された定義は **Missing / Unknown** と表示し、自動置換せずValidate / Exportでエラーにします。

### 8. Spawn Triggerを置く

開始時刻で敵を出す場合は、下記の「時刻指定のSpawn Group」を使用してください。既存のSpawn Triggerは引き続きプレイヤーの進入で発火します。

1. **Shift+A → Empty → Cube** を追加します。
2. Object Typeを **Spawn Trigger** にします。
3. **Spawn Group** を対応するEnemy Spawnと同じ `A` にします。
4. Cube Emptyを移動・拡縮し、プレイヤーが通る場所を囲みます。

同じGroupのEnemy Spawn IDをExporterが自動収集します。TriggerへSpawnPoint IDを列挙する必要はありません。同じGroupにEnemy Spawnが0個ならExportエラーです。

Trigger／Goalは既存の軸平行AABBです。斜め回転はエラーにします（90度単位の回転は対応）。Cube EmptyはDisplay SizeとScaleを含む実際の箱サイズを使用します。MeshのCubeも使用できます。

### 時刻指定のSpawn Group（Sequential / Simultaneous）

1. **Shift+A → Empty → Arrows** を追加し、Object Typeを **Spawn Group** にします。
2. **Mode**をSequentialまたはSimultaneousにします。
3. **Start Time**へゲーム開始からの秒数を指定します。
4. **Add Enemy**で行を追加し、既存の敵定義メニューからEnemy、オブジェクト選択欄からEnemy Spawnを選びます。
5. Sequentialでは**Interval**を指定します。上下ボタンで出現順を変更でき、マイナスボタンで行を削除できます。

Sequentialは`Start Time + 行番号 × Interval`で出現します。たとえばStart Time=3、Interval=0.5なら3.0秒、3.5秒、4.0秒です。
Simultaneousは全行をStart Timeに同じフレーム内で出現させます。Interval欄は編集不可になり、保存済みの値は実行時に使用しません。
各行の敵定義はEnemy Spawnの抽選Poolより優先され、そのSpawnPointの位置と回転を使用します。同じSpawnPointを複数行で使用することもできます。

Enemy Spawn側の従来の**Spawn Group**文字列は、Spawn TriggerがSpawnPointを収集するための設定です。
時刻指定のSpawn Groupは各行の参照から敵を決めるため、その文字列と関連付ける必要はありません。
両方から同じSpawnPointを参照すると、それぞれの設定で敵が出現します。
Spawn Groupオブジェクト自身の位置・回転は出現場所に影響せず、glTFにも出力しません。

Exportは既存JSONへ任意の`spawnGroups`配列を追加します。既存の`spawnPoints`／`spawnTriggers`は維持します。
設定例（`normal`／`fast`／`tank`は既存カタログのID、Spawn01〜03はEnemy SpawnのID）:

```json
"spawnGroups": [
  {
    "id": "Wave01",
    "time": 3.0,
    "mode": "Simultaneous",
    "interval": 0.5,
    "enemies": [
      { "enemy": "normal", "spawnPoint": "Spawn01" },
      { "enemy": "fast", "spawnPoint": "Spawn02" },
      { "enemy": "tank", "spawnPoint": "Spawn03" }
    ]
  }
]
```

JSONで`mode`を省略した場合はSequential、`time`は0秒、`interval`は0.5秒が既定値です。
`id`を省略した場合は配列順に`SpawnGroup_1`などを使用します。IDは他のステージ要素と重複させないでください。
敵が空、未知の敵定義、存在しないSpawnPoint、不正な時刻・間隔は読み込み／Exportエラーです。
時刻指定だけのJSONでは`spawnTriggers`を省略できます。既存データに`spawnGroups`がない場合は従来通り動作します。

ゲームプレイ中の時間だけが進み、Debug Pauseでは停止します。低フレームレートで複数のSequential出現時刻を通過した場合は、そのフレームに期限を迎えた敵をまとめて出します。
各グループはステージにつき1回発火し、Restart Stageで時計と出現済み状態をリセットします。Debugの巻き戻しでもこれらの状態を復元します。
取得は既存EnemyPoolのAcquire／ResetForSpawn経由です。Simultaneous全員の取得とActive化をAI更新前に終え、容量不足は既存Poolの拡張処理へ任せます。
Spawn TriggerのMax Aliveは時刻指定のSpawn Groupには適用しません。

追加検証:

```powershell
./Tools/test-blender-spawn-groups.ps1
./Tools/test-enemy-spawn.ps1
./Tools/test-stage-loader.ps1
./Tools/test-enemy-pool.ps1
./Tools/build.ps1 -Configuration Debug
./Tools/build.ps1 -Configuration Release
```

Blenderの専用テストは既存stage01.blendを読み、テスト用グループの編集・カタログ選択・順序変更・保存・再読込・ExportとC++スケジューラーへの読み込みを確認します。
生成物は`generated/spawn-group-tests`へ出力します。D3D Poolテストは実際のGameSceneで同一更新内の全員Active化、位置・回転・敵定義、容量不足の拡張、AI・HP・部位破壊・死亡・返却・再利用、巻き戻し、RestartのSceneライフサイクルを確認します。

### 9. 敵数とタイミングを設定する

|項目|例|意味|
|---|---:|---|
|Spawn Count|8|このTriggerが出す総数|
|Spawn Interval|0.5|敵を出す間隔、秒|
|Initial Delay|0|Triggerへ入ってから最初の敵まで、秒|
|Max Alive|4|このTriggerの敵の同時生存上限|
|Selection|Random|スポーン地点の抽選|
|Selection|RoundRobin|ID順に並べたスポーン地点を順番に巡回|
|One Shot|ON|一度だけ発動|

地点の選択と、その地点のEnemy Pool抽選は別です。RoundRobinでも、1地点に複数IDのPoolがある場合は敵種類を抽選します。

サンプルの最初のAエリアは、5地点それぞれに1種類だけを設定し、RoundRobin・総数5・Max Alive=5にしてあります。Normal → Ranged → Fast → Tank → Bomberを必ず各1体確認できます。

### 10. Weapon Spawnを置く

1. Emptyを追加し、Object Typeを **Weapon Spawn** にします。
2. IDと位置・回転を設定します。
3. 個別の候補とWeightを設定する場合は **Add Entry** から **Weapon Pool (Manual Pool)** を追加します。

例：pistol=3、smg=2、rifle=2、pump_shotgun=1。選択メニューは `resources/Data/weapons.json` から生成し、名前・ID・Slot・★Rarity・Typeを表示します。Weightの編集とマイナスボタンでの行削除ができます。武器の見た目・取得方式・弾数は既存WeaponSystemが担当します。

**Weapon Filter** でSlot（All / Main / Sub）、Min Rarity / Max Rarity（★1～★5）、Typeを指定します。Typeのチェックは複数選択でき、候補はweapons.jsonから取得します。選択なしはAll、Allボタンで選択を解除します。複数Typeはどれかに一致すれば対象です。削除されたTypeも警告付きで保持され、クリックして解除できます。

**Matched Weapons** に実際の抽選元をFilterで絞り込んだ武器数と一覧を表示します。Manual Poolがあればその中だけを絞り込み、各行のWeightで抽選します。Manual Poolが空なら全定義を絞り込み、武器定義のWeight（省略時1）で抽選します。候補0件または候補のWeight合計0は警告・検証エラーです。

Exportは既存の `filter.slot` / `minRarity` / `maxRarity` / `types` を使います。SlotのAllはslot省略、TypeのAllはtypes省略です。Manual Poolが空の場合は `weaponPool` 自体を省略し、ゲームの全武器抽選を使用します。既存.blendではFilter初期値がAll・★1～★5なので、従来のPoolとWeightをそのまま使います。

#### Weapon CatalogでManual Poolを作る

Weapon Spawnを選択して **Open Weapon Catalog** を押すと、武器ID・★Rarity・Slot・Typeを表示するスクロール可能なカタログを開きます。武器とTypeはProject Rootのweapons.jsonから読み込みます。

1. カタログのSlot / Rarity / Typeで表示を絞り込みます。これは検索用で、SpawnのDynamic Filterには保存されません。
2. 各武器をチェックします。既存Manual Poolの武器は最初からチェックされています。**Select All Visible** は表示中だけ選択、**Clear Visible** は表示中だけ解除、**Clear All** は非表示分も含めて解除します。
3. **Apply to Weapon Spawn** で、非表示分を含むすべてのチェックをManual Poolへ反映します。チェックを外したIDはPoolから外れ、新規IDはWeight=1になります。残したIDは既存Weight・行順を維持します。Dynamic Filterは変更しません。ApplyはUndoで戻せます。

Applyせずにポップアップを閉じればSpawnは変更されません。再度開くと現在のManual Poolから選択を作り直します。全チェックを外してApplyすると空Manual Poolになり、ゲームでは全武器をDynamic Filterで絞り込む方式になります。

**View Matched in Catalog** は、現在保存されているManual PoolとDynamic Filterを併用した実際の候補だけを表示します。**Only Spawn Matches** を外せば全武器の閲覧に戻れます。表示Filterを追加すると、その候補をさらに絞れます。カタログ内のチェック変更だけではMatchedの対象は変わりません。

JSON変更後はカタログの **Reload** またはカタログを開き直すことで更新できます。Reloadは既存チェックを保持し、新しい武器は未選択で追加します。削除された選択済みIDは **Missing / Unknown** として残ります。既存Poolの不明IDも勝手に削除せず、チェックを外してApplyした場合に削除します。

Dynamic Filterは条件を保存するので、Manual Poolが空なら将来追加された一致武器も候補になります。Catalogで作ったManual PoolはIDを保存するため、将来追加された武器は自動追加されません。

### 11. Goalを置く

Cube Emptyを追加し、Object Typeを **Goal** にします。ゴールにしたい場所を箱で囲みます。拡縮がそのままゲームのGoal範囲になります。サイズ0はエラーです。Goalへ入ると既存StageProgressのStageClearになります。

### 12. Validate Levelを押す

パネル下部の **Validate Level** で確認します。エラー時はBlenderのメッセージを確認し、該当Objectを修正してください。

Player Spawn数、Duplicate ID、Group参照、Enemyの空Pool・全Weight=0、不明ID、Min Rarity > Max Rarity、Filter後に正のWeightの武器候補がない場合、体積0、NaN/Infinity、Gameplay Transformのシアーなどを検証します。**Ignore** のObjectはゲーム用の出力・検証対象から除外します。

### 13. Export Levelを押す

Object Modeで **Export Level** を押します。Edit Modeでは実行しません。

出力：

- `resources/levels/stage01/stage01.gltf`：Static Meshの見た目。
- `resources/levels/stage01/stage01.bin`：glTFの頂点など。glTFと一緒に保管してください。
- `resources/levels/stage01/stage01.json`：PlayerSpawn・Collider・Enemy／Weapon Spawn・Trigger・Goal。
- 必要な場合はglTFが参照するテクスチャ。

一時フォルダーに全て出力・検証してから、フォルダーを差し替えます。置換失敗時は元のフォルダーへ戻します。出力先にある.blendなどの無関係なファイルは保持します。Blender自身の作業フォルダーが出力先の中にある場合は、差し替え中だけ外へ移してWindowsのフォルダーロックを解除し、処理後に元へ戻します。OS停止・電源断まで含む完全なファイルシステム取引ではありません。差し替えの瞬間に停止した場合は、隣の `.stage01-backup-*` が旧出力の復旧元になります。

Export後は.blendもCtrl+Sで保存します。**ゲームを終了して起動し直す**と、モデルキャッシュも更新され、変更したStageを読み込みます。起動中のExportによるホットリロードは実装していません。ゲーム内Restartは現在のステージの再開始に使用してください。

## ゲーム側の実装と範囲

GameSceneはStage01のJSONをStageLoader・EnemySpawnSystem・WeaponSystem・StageProgressへ共通で渡します。各設定がすべて読み込めた時点で採用するため、Stageだけ新しくSpawnだけ古い、という部分状態を避けます。正常時には旧巨大Cube床を描画しません。読み込みに失敗した場合はデバッガへ理由を出し、既存fps_spawns.jsonと旧Cube床へFallbackします。

fps_spawns.jsonは互換テスト用に残しています。普段の編集元はstage01.blendです。create_stage01.pyは初回サンプルの再生成用であり、通常のExportにfps_spawns.jsonを使用しません。独自編集後にこの生成スクリプトを実行するとサンプルへ戻るので、通常は使わないでください。

PlayerはXZ移動のCylinderをColliderのローカル軸へ投影してSweepし、壁で停止・Slideします。角では保守的な判定です。敵移動も同じ判定を使い、壁を自動で迂回するNavMeshやPathfindingはありません。壁に阻まれて止まることがあります。

PlayerのHitscanは最寄りStage Colliderまでに制限します。Ranged弾は壁で消え、BomberのBombは曲線を細分化して壁・床へSweepし、接触位置で停止してFuse後に爆発します。高度な物理・バウンドは実装していません。側面に当たったBombもその接触位置で停止します。爆発は従来どおり距離判定で、爆風の遮蔽物判定は追加していません。

Debug ImGuiのSpawn System画面で **Show Stage Colliders** をONにすると、Sceneへ黄色のCollider枠を重ねます。回転を含む8頂点を表示します。

## 座標変換の根拠

BlenderのglTF ExportをY-upで実行すると `(x,y,z) → (x,z,-y)`。既存Model.cppはglTFの頂点とNode TransformのXを反転するため、ゲーム側の点は `(-x,z,-y)` です。アドオンの `ENGINE_BASIS` と `engine_transform()` に変換を集約しています。

Geometryは評価済みWorld Transformをメッシュへ焼き込み、Colliderは `C × BlenderWorld × C⁻¹` と `C × LocalBounds` に分けて出します。C++はScale・XYZ Euler・PositionからWorldを再構築します。Parentを含む回転・非均一Scaleでも、この2経路の結果が一致することを自動テストしています。シアーは曖昧に分解せず拒否します。

参考：[Blender公式glTF Export API](https://docs.blender.org/api/main/bpy.ops.export_scene.html)。

## 自動検証と未確認項目

Debug / Release x64：両方とも警告0・エラー0。C++自動テストは既存7スイート＋StageLoaderTestsの計8スイートが成功しています。

実行用スクリプト：

- `tools/test-stage-loader.ps1`：Stage JSON、Transactional Load、資産、回転Box、表示／Collider座標、壁沿いSlide、Hitscan遮断、弾・爆弾、既存Spawn／Weapon／Goalとの互換性。
- `tools/test-blender-level.ps1`：Blenderをバックグラウンド起動。必要なら `-BlenderPath` で実行ファイルを指定。
- 既存のEnemyParts／EnemySpawn／EnemyTypes／StageProgress／Weapons／FPS／Raycastテストも実行。

Blender 5.0.1でアドオン登録・Export・Role別除外・保存プロパティの復元・不正値拒否・置換失敗時の復旧を確認しました。Python Syntax Checkも実行しています。Blender 4.4実機での確認は未実施です。

ゲーム画面・Blender画面を操作した目視確認は行っていません。以下は手元で確認してください。

- [ ] アドオンを導入し、NパネルのYanEngine Levelが表示される。
- [ ] Object Typeを切り替えると、対応する項目だけ表示される。
- [ ] Enemy／Weapon Poolを追加・削除でき、保存・再読込後も設定が残る。
- [ ] Blenderで変更した床・壁・装飾が、Export後のゲーム再起動で同じ位置に表示される。
- [ ] Collider／Spawn／Trigger／Goal用オブジェクトそのものはゲームに描画されない。
- [ ] Playerが指定の足元位置・向きから開始し、Restartでも戻る。
- [ ] 壁へ正面移動しても抜けず、斜め移動では壁に沿って進める。
- [ ] Collision Noneの装飾は移動・射撃を遮らない。
- [ ] 回転壁の表示とDebug Collider枠が重なる。
- [ ] 壁の向こうの敵に射撃ダメージが届かない。
- [ ] Ranged弾が壁で消え、Bombが壁・床で停止してFuse後に爆発する。
- [ ] 敵が壁を通り抜けない（迂回AIはありません）。
- [ ] TriggerのGroup・Spawn Count・Max Alive・間隔と、敵種類のPoolが反映される。
- [ ] 武器とGoalがBlenderで指定した位置にあり、取得・StageClearが動く。
- [ ] 既存の体格・Marker・被弾色・部位破壊が維持されている。


## Approximate Collision / Box Compound（1.4）

Static Meshの **Generate Approximate Colliders** は、評価済みメッシュ（Boolean等を含む）の面分布から
複数のBoxを近似生成します。ゲーム側のMesh Colliderは不要です。

- **Max Box Count**：生成数の上限です。単純な直方体は1個で止まります。既存の保存プロパティ
  `auto_collider_count` は維持しているため、以前の.blendの設定値をそのまま利用できます。
- **Padding**：分割後の各Boxに加えるローカル空間の余白です。Box間の隙間を守るため、
  向き合う面の余白は隙間の1/4以下に制限します。接する面には余白を追加しません。
  これにより元の隙間の少なくとも半分を残します。外向きの余白は指定値を使います。

### 分割方法

X/Y/Zすべてについて、三角形中心の大きなGapと面の境界座標を分割候補にします。
候補平面で面を一時的にクリップし、子グループのBox総体積がどの程度減るかを比較します。
元メッシュは変更しません。

門型や奥行きのある入口では途中の分割だけでは体積が減らないため、最大4分割先まで候補を保持します。
探索はMax Box Countの範囲内で行い、最初の分割位置ごとに最大8候補を保持します。全候補を一括で絞らず、小さい入口の途中候補を残します。
最終的に体積が改善する分割だけを採用します。小さい入口を無視しないよう、
1%/追加Boxのペナルティを廃止し、数値誤差程度の改善だけを除外します。UI項目は増やしていません。
候補数は各軸の主要なGap・境界に制限しています。曲面・複雑な形状では最適解を保証するものではなく、
Max Box Countが少ない場合は通路を完全に残せないこともあるため、必要に応じて手動調整してください。

### 生成・再生成

生成物は従来どおり `COL_<SourceName>_00` 等のCube Empty / Collider Objectです。
移動・回転・拡大縮小による調整、`yan_auto_collider_generated`、`yan_auto_collider_owner`を維持します。
再生成／ClearはそのSourceの自動生成分だけを削除し、手動Colliderと別Sourceの生成物を残します。
再生成の形状計算・入力検証に失敗した場合は、前の生成物を維持します。

### 検証

Blender **4.4.1** と **5.2.2 LTS** のバックグラウンドテストで、Max Box Count=8に対して確認しました。

| 形状 | 生成数 |
|---|---:|
| Cube | 1 |
| 横長直方体 | 1 |
| L字（接続した凹形状） | 2 |
| 門型（接続した凹形状） | 3 |
| Booleanで入口を開けた門型 | 3 |
| 離れた2形状を1Meshにしたもの | 2 |

門型は上部と左右の柱に相当する3Boxへ分かれ、X/Y/Z方向を入れ替えても中央を塞ぎません。
Padding 0.02および10でも中央の通路が残ること、Max Box Countの遵守、床／棚の面の保持、
回転・非均一スケール、手動調整、再生成／Clearと手動Collider保護を確認しています。
実際のExportを既存StageLoader/StageWorldに読み込み、中央の射線・プレイヤー移動が通過し、
柱・上部で遮られることも確認しています。

C++実装とLevel JSON形式は変更していません。Colliderの出力は引き続き
`id / position / rotation / scale / localBounds`です。

```powershell
./Tools/test-compound-colliders.ps1 -BlenderPath 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
./Tools/test-blender-level.ps1 -BlenderPath 'C:/Program Files/Blender Foundation/Blender 5.2/blender.exe'
```

テスト用のメッシュ、JSON、glTFは`generated/compound-tests`にのみ出力します。

既存Level Exporter全体の回帰テスト（Static/Custom Collider、Spawn、Trigger、Goal、
Catalog、保存プロパティ、Export失敗時の復旧）は5.2.2で成功しました。
4.4.1では現行の`stage01.blend`を読み込めず、この既存ファイルを使う回帰テストは開始できません。
今回追加した形状生成・Export・StageWorldの専用テストは、4.4.1でも成功しています。
ステージファイルの変換や保存し直しは行っていません。

1.4.3: 分割前の頂点座標から構造上の境界候補を固定し、クリップで生じた三角形の交点が入口の候補を押し出す問題を修正。保存された入口付きメッシュを独立した回帰テストへ追加。Max Box Count 4で左右・上部・奥の4 Box、Padding 0.02でも入口内部を覆わないことを確認。
