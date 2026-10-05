# 移動できるFPSタイトル画面

TitleSceneは `resources/levels/title/title.json` を読み込みます。`title.blend` の既存配置を維持し、GAME STARTの9文字を `GAME_START` という1つのMesh Objectへ結合しました。

## 操作と動作

- WASDで移動、マウスで視点操作、左クリックで射撃、Rでリロードします。移動・壁の衝突処理は既存Player / StageWorldと共通です。
- ESCでマウスを解放。DebugではScene画像をクリックするとFPS操作に戻り、その復帰クリックでは発砲しません。フォーカスを失った間は更新を止めます。
- 左側のEnemyは既存Enemy / EnemyPoolで管理。AIと攻撃を止め、部位ダメージ・色変化・部位破壊・死亡・破片を更新します。破片が消えた後に再出現します。
- GAME STARTは**1つのモデル、1つのHead部位**です。文字表面のどこに命中してもHead判定となります。文字の穴や文字間の空白はメッシュ判定により外れます。
- HeadのHPは1で、1発の射撃で既存 `Enemy::ApplyBulletDamage` → `Enemy::Die` → `SpawnFaces` が動きます。文字が丸ごと飛ぶ方式から、Enemyと同じ三角形の面破片が散って回転・落下する方式へ変更しました。文字の色・破片の表現も既存Enemyの描画を使います。
- 爆散開始後は移動と追加射撃を止め、0.45秒の演出後に共通SceneManagerのFadeOutを開始。0.75秒で黒くなり、黒フレームを描画してからStage01を読み込み、0.75秒のFadeIn後にゲームを開始します。FadeIn中は操作・敵・ステージ時間を停止します。
- 壁や左側Enemyが手前にある場合はそちらに先に命中します。左側Enemyを倒してもゲームは開始しません。プレイヤーへのダメージはありません。
- 弾倉・予備弾が両方ゼロになった場合、タイトル用武器を再補充します。通常のリロードは既存WeaponRuntimeを使います。

## Blenderの設定

1. 更新した `tools/blender/yanengine_level_exporter.py`（v1.7）をBlenderアドオンとして再読み込みします。古い版は無効化してから更新してください。
2. `resources/levels/title/title.blend` を開きます。Nパネル **YanEngine Level** の **Stage ID** は `title`、**Project Root** は `CG2_Setup.sln` があるフォルダー、**Output Directory** は `resources/levels/title`。サンプルはProject Rootを `.blend` 基準の `//../../../` に設定済みです。
3. GAME START全体を**1個のMesh Object**として作ります。Textなら Object → Convert → Mesh。複数Objectなら選択して **Ctrl+J** で結合します。Object名の例は `GAME_START`。
4. そのObjectの **YanEngine Object Type** を **Game Start (Head)** に設定します。全ポリゴンを自動的にHeadへ割り当て、HP 1・死亡時破壊の既存EnemyAssetを出力します。Enemy Partsアドオンによる手動の頭部割り当てや、文字ごとの分割は不要です。Object名は任意ですが、このRoleのMeshは1個だけにしてください。
5. **Player Spawn** のEmptyを1個置いて初期位置・初期視線を設定します。足元位置から既存Playerの視点高さ1.6mが加算され、開始後はそこからWASDで移動できます。
6. 左側EnemyのEmptyを1個、**Enemy Spawn** に設定します。例：`EnemySpawn_Title`。**Enemy Pool** は標準 `normal`、Weight 1。Spawn Groupは `Title` など任意、Spawn Triggerは不要です。
7. タイトル、土台、`SHOOT TO START`、床・壁は **Static Mesh**。土台・壁には **Collision: Box** または既存Custom Colliderを設定すると、移動と弾が遮られます。GAME START自体には別Colliderを重ねないでください。
8. **Title Weapon ID** は標準 `pistol`。**Explosion Hold Before Fade** は0.4～0.5秒（標準0.45秒）。JSONには `title.explosionDelay` として出力します。古い `startDelay` は使用せず、旧JSONでは0.45秒を使います。FadeOut / FadeInは各0.75秒です。
9. **Validate Level** → **Export Level** を実行して `.blend` を保存します。

背景は `title.gltf`、GAME STARTは `game_start.enemy.json` へ出力します。後者は既存Enemy用のモデル・部位データ形式で、全文字の形状を1つのHeadメッシュとして持ちます。タイトルではその1モデルを生成し、個別文字のglTFを読み込みません。位置・回転・スケール・親変換はエクスポーターがベイクします。

以前の `Game Start Letter` 設定はアドオン更新後に `Game Start (Head)` と表示されます。複数文字Objectが残る旧ファイルはCtrl+Jで結合してください。今回のサンプルは結合済みです。旧エクスポーター生成の `start_00.gltf/.bin` などは新しいExport Levelで整理されます。

## 実装と検証

射撃とリロードは `Player::UpdateShooting`、弾道と命中は `BulletManager`、GAME STARTのHeadダメージ・死亡・爆散・破片描画は既存Enemyを使用します。TitleStartSequenceは爆散の待ち時間のみを管理し、フェードと切替は共通SceneManager / FadeManagerを使用します。再利用方法は [scene-transitions.md](scene-transitions.md) を参照してください。

```powershell
./tools/build.ps1 -Configuration Debug
./tools/build.ps1 -Configuration Release
./tools/test-title-scene.ps1
./tools/test-fps.ps1
./tools/test-blender-title.ps1
./tools/test-blender-level.ps1
```

タイトル専用テストは実D3Dで1つのHeadモデル、各文字のHead命中、遮蔽、1発での死亡、既存Enemyの面破片生成・回転、GPUリソースの命中時追加生成がないこと、0.45秒の待機、黒フレームを挟む遷移、Stage01のFadeIn中の停止と再開を検証します。結果と描画画像は `generated/title-tests/` に保存します。

`tools/blender/merge_title_start.py` は既存サンプルを読み込み、配置を保持して文字を結合・再出力します。`create_title.py` は仮配置を初期状態から作り直すためのスクリプトで、編集済みのtitleアセットを上書きするため通常の編集にはExport Levelを使ってください。
