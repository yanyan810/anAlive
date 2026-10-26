# ゲームオーバー演出

2026-10-26追加: 死亡専用の暗赤→黒ビネット、彩度低下、周辺Blur、短い色収差を追加しました。演出時間・敵・カメラ・UIは維持しています。設定は `Settings.post`、詳しい実装・負荷・復旧の記録は [GAME_OVER_POST_EFFECTS.md](GAME_OVER_POST_EFFECTS.md) を参照してください。

## 動作

HP が 0 になると GameScene が専用の `GameOverSequence` に制御を渡します。
通常の GameOverScene への即時切り替えは行わず、読み込んだステージを背景として保持します。

1. Death / 硬直 (0.3 秒): 死亡座標・カメラ姿勢・被弾方向を記録し、入力、射撃、リロード、拾得を直ちに停止。立った身体を保って少し踏ん張り、視点を最大0.06下げながら小さく揺らします。揺れは両端で0になります。既存の敵だけ 0.15 倍速で動き、追加のダメージは適用しません。
2. Collapse (1.2 秒): ゆっくり倒れ始め、途中から加速して、接地前に減速。FPS カメラを床から0.24の高さへ下げ、ローカル前方軸を中心に傾けます。Death / Collapse 中は暗転・閉眼せず、視界を保持します。
3. FloorHold (0.3 秒): 接地時に小さな上下・回転の揺れを0.18秒かけて収め、床付近の視界を短く保持します。
4. Closing (0.7 秒): 上下の曲線状のまぶたを閉じます。Ease-outで閉じ終わりに向かって減速し、暗さも閉じ具合に合わせて増します。
5. Corpse (1.2 秒): 完全に目を閉じた暗闇と、床付近の倒れたカメラ姿勢を保持します。
6. Opening (1.8 秒): 曲線状の上下まぶたを一度だけゆっくり開きます。途中で閉じ直さず、全開後もまばたきをしません。
7. Awake (0.6 秒): 全開の目で床付近の視界を完全に静止させます。
8. Rising (3.2 秒): 最初にわずかに頭を動かし、まだ低い位置で静止してから身体を起こします。全区間に既存のカメラ衝突処理を適用します。内部の敵化用個体は同じオブジェクトです。
9. Possess (1.1 秒): 目の位置を維持して揺れを徐々に加えます。自身の全身モデルは全区間で描画せず、距離による表示切り替えを行いません。
10. Wander: 既存 EnemyAI の Wander / EnemyNavigation を使用して自動徘徊。初期の視界の揺れは減衰します。この時点（初期値で死亡から10.40秒後）から従来どおり GAME OVER、Retry、Title と背景の薄暗さをフェード表示します。

メニューは上下キー / W・S で選択し、Enter / テンキー Enter / Space で決定します。
各文字列の周囲もクリックできます。死亡中から保持されていた決定キー・マウスボタンは、離すまで受け付けません。
Debug の Scene ビューポートと Release のウィンドウの双方で座標変換します。

Retry は現在の `stageId` を保持した新しい GameScene を生成し、ステージファイルの開始地点・HP・敵・武器・進行状態から再開します。
Title と Retry は既存 SceneManager の 0.5 秒フェードアウト / 0.5 秒フェードインを使用します。
途中のステージ再読み込みでも OnExit で演出個体、AI、演出時計を破棄し、カメラの近クリップ/FOV、マウス設定を復旧します。
通常の開始カメラが設定されているステージでは、再開時にもその演出が動きます。

## 流用と独立性

- 仮モデルは既存 StartPlayerVisual と ModelManager の Normal 形状キャッシュを使用。通常敵の部位HP・部位破壊・AI設定は変更しません。
- EnemyPool / `enemies_` に演出個体を登録しません。出現数、撃破数、クリア条件に影響しません。
- 演出中は通常のゲーム更新を早期 return。敵出現、ゲーム時計、通常のクリア判定、弾の更新、プレイヤー入力は止まります。
- 遅延したクリア通知も OnStageClear のガードで拒否します。既存の「生存中にゴールへ到達すると、そのフレームの戦闘前にクリア」の優先順は維持しています。
- 倒れ込み・起き上がりはキネマティックな簡易アニメーションです。StageWorld の床・重力処理と回転したモデル境界を使って接地させます。
- カメラは死亡時の位置・姿勢と足元から計算し、水平移動は通常 0.16 以内です。回転順 X*Y*Z に Z 角度を足すと世界軸まわりに回転するため、ローカル前方軸の回転を合成してエンジンの Euler 角へ戻します。ほぼ真上・真下を見て死亡しても、ピッチが垂直を超えて視線が反転しないよう制限します。
- `FirstPersonCamera` の半径 0.12 の球を、直前のカメラ位置から時間サブステップごとにスイープします。床・天井・回転した壁・プレイヤー専用ゲートを含めて接触面に沿わせ、初期重なりも解消します。徘徊も同じカメラ衝突処理を通します。
- GAME OVER 中だけ、カメラから 0.16 以内に入った通常敵のメッシュ・破片の描画を抑えます。移動用の円柱ではなく、アニメーション後の描画境界で判定します。敵の HP・AI・位置・部位破壊状態は変更しません。
- 徘徊は既存の床付き経路探索と円柱のスイープ衝突で移動します。
- 時間倍率は演出のローカル値であり、アプリ全体の時計は変更しません。状態の境界をまたぐフレームは残り時間を次の状態へ渡します。物理・AIの積分間隔は最大 1/60 秒です。
- 硬直後の軽く下げた視点から倒れ込みを開始します。加速・減速カーブ `t^n * (n+1-n*t)` は両端で速度0となり、死亡体の傾きにも同じ進行率を使います。接地の揺れは接地後の専用経過時間で計算し、待機を短くして閉眼へまたいでも途中で切れません。揺れを加えた後のカメラ位置にも既存の球スイープを適用します。
- 意識喪失・開眼は `GameOverVisionOverlay` が担当し、SceneManager の FadeManager には書き込みません。描画順はステージ → GAME OVERメニュー（Wanderのみ）→ 意識・まぶた → SceneManagerの遷移フェードです。Retry / Title の既存フェードが常に最前面です。
- まぶたは既存 `Sprite` の四角形を上下各32分割した曲線です。既存の白1ピクセルテクスチャを黒く着色し、追加画像・シェーダは不要です。0〜1の正規化座標と正射影により描画先の解像度・縦横比全体を覆います。
- オーバーレイは独自の時計を持たず、`GameOverSequence::Vision()` の結果だけで描きます。終了・Retry・途中のシーン変更でシーケンスをResetすると暗さ0／全開へ戻り、旧シーンのGPU資源は既存SceneManagerの安全な破棄経路に従います。

## 調整・拡張

`Game/GameOverSequence.h` の `Settings` に以下をまとめています（秒・距離はゲームのワールド単位）。

| 項目 | 初期値 | 用途 |
| --- | --- | --- |
| staggerSeconds | 0.3秒 | 死亡直後、立った姿勢で踏ん張る硬直時間 |
| collapseSeconds | 1.2秒 | 硬直後の倒れ込み時間 |
| staggerDip / staggerPitch | 0.06 / 0.025 rad | 硬直の終わりまでに下げる視点の距離・角度 |
| staggerShake / staggerShakeHz | 0.012 rad / 10 Hz | 硬直中の小さな揺れの振幅・周波数 |
| collapseCurve | 3 | 倒れ込みカーブの指数（2〜5）。大きいほど遅い始動から後半に加速 |
| landingSeconds | 0.18秒 | 接地時の揺れが収まるまでの時間。0で無効 |
| landingShake / landingLift / landingShakeHz | 0.03 rad / 0.012 / 12 Hz | 接地の回転揺れ・上下揺れ・周波数。両端0の包絡で実際の振幅を抑制 |
| floorHoldSeconds | 0.3秒 | 倒れ込み完了後、視界を維持する時間 |
| closeSeconds | 0.7秒 | まぶたを閉じる時間 |
| corpseSeconds | 1.2秒 | 完全に閉眼した後の暗闇の時間 |
| openSeconds / awakeSeconds | 1.8 / 0.6秒 | 1回の開眼・全開後に静止する時間 |
| closeCurve | 3 | 閉眼の減速カーブ `1-(1-t)^closeCurve`（1〜4、1で等速） |
| openCurve | 1.1 | 単調な開眼の時間カーブ（時間を指数変換してSmoothstep、0.5〜4） |
| eyelidCurve | 0.75 | まぶたの曲率（0で水平、0〜2） |
| riseSeconds / possessSeconds | 3.2 / 1.1 | 一人称での起き上がり・起立後の揺れ |
| riseHeadEnd | 0.16 | Rising開始から小さな頭の動きが終わるまでの割合（初期値0.512秒） |
| risePauseStartSeconds | 0.55秒 | Rising開始から静止開始までの時間 |
| risePauseSeconds | 0.45秒 | Rising中の静止時間（初期値で0.55〜1.00秒） |
| riseHeadLift / risePauseHeight | 0.04 / 0.04 | 頭の動き終了時・静止時の高さの進み具合。身体を起こす前の低い目線 |
| riseCurve | 1.4 | 起き上がり区間の緩急（0.5〜4） |
| riseHeadPitch / riseHeadRoll | 0.055 / 0.045 rad | 頭を動かす区間の一時的な上下・傾きの振幅 |
| slowMotion | 0.15 | 硬直区間中の既存の敵の時間倍率 |
| floorEyeHeight / fallDistance | 0.24 / 0.16 | 床付近の目線高・死亡時の右方向を基準にした横移動 |
| cameraRadius / fallenRoll | 0.12 / 1.25 rad | カメラ球の半径・倒れたときのローカルロール |
| shake / settleSeconds | 0.065 rad / 3 | 接続時の揺れと減衰 |
| wanderSpeed / wanderRadius | 1.05 / 6 | 演出個体の徘徊速度・範囲 |
| collisionRadius | 0.32 | プレイヤーサイズの演出個体の半径 |
| pose | FromImpact | 被弾方向から選択 / Supine / Prone / LeftSide / RightSide |

姿勢の追加は `DeathPose` と Begin の姿勢選択に追加できます。死亡原因や専用プレイヤーモデルが追加されたら、DeathContext と StartPlayerVisual のモデルバインドを拡張してください。
メニューの色・レイアウト・クリック領域は `Game/GameOverOverlay.h` にあります。
状態の時間は有限値かつ最低0.01秒に補正します。起き上がりの一時停止時間は0秒で停止なしにできます。開始位置はRising全体の2〜95%へ、再開位置は98%までに制限し、最後に身体を起こす区間を残します。頭の動きは停止開始までに終わるよう制限します。`MenuDelay()` は補正後の各状態の合計時間を返します。

今回、旧 `fadeCurve` と、まばたき専用の `firstOpenAt / blinkAt / firstOpening / blinkOpening` を削除しました。旧 `risePauseStart / risePauseEnd`（割合指定）は、開始秒数と停止秒数を個別に設定する `risePauseStartSeconds / risePauseSeconds` に置き換えました。
硬直の調整では、旧 `deathSeconds` を `staggerSeconds` に置き換え、旧 `deathRoll` は廃止しました。硬直で大きく傾く動きは行わず、小さな揺れと視点低下だけを加えます。

### 今回の硬直・倒れ込み・閉眼調整で変更したファイル

- `Game/GameOverSequence.h`, `.cpp`: Death区間を立った硬直へ変更、倒れ込みの加速・減速、接地の揺れ、待機・閉眼の短縮。各時間・振幅・カーブのSettings。
- `tests/GameOverSequenceRuntimeTests.h`: 立った姿勢と小さな揺れ・視点低下、加速と接地前の減速、接地の揺れと収束、短い待機から閉眼へまたぐ連続性、30/60/144 FPS、描画、既存機能の回帰検証。
- `docs/GAME_OVER_SEQUENCE.md`: 現在の時間・設定・検証結果。

敵の位置、通常AI、描画仕様、死亡姿勢、FirstPersonCamera、EnemyNavigation、Wander、GAME OVERメニューとRetry / Titleは今回変更していません。

### 以前の意識喪失・開眼の追加で変更したファイル

- `Game/GameOverSequence.h`, `.cpp`: Opening / Awake状態、意識喪失時間、視界の評価、段階的な起き上がりと各Settings。
- `Game/GameOverVisionOverlay.h`: 新規。暗さと曲線状の上下まぶたをSpriteで描画。
- `Game/scene/Main/GameScene.h`, `.cpp`: 専用オーバーレイの初期化と描画。
- `Game/GameOverOverlay.h`: メニューはWanderから描画し、背景シェードも滑らかにフェード開始。
- `Engine/2D/Sprite.h`, `.cpp`: `SetQuad`で任意の四角形を指定可能に拡張。既定の四角形、アンカー・反転・UVは従来どおり。
- `tests/GameOverSequenceRuntimeTests.h`: 全状態・暗さ・まばたき・静止・30/60/144 FPS・4解像度のGPUピクセル検証・途中破棄・Retry / Title。
- `tests/TemporarySceneTransitionCheckTests.h`: 新しい演出時間に`MenuDelay()`で追従。
- `CG2_Setup.vcxproj`, `.filters`: 新しいヘッダーを登録。
- `docs/GAME_OVER_SEQUENCE.md`: 動作、設定、検証方法。

## 未完成・制約

- 関節付きラグドール、手足を曲げる起き上がり、専用の死亡モーションは未実装。Normal の静的な形状を全体として回転させます。
- 死亡体の接地はモデル境界による近似です。狭い場所では手足が壁や段差に重なる場合があります。地形に合わせた関節補正・IKはありません。
- 死亡姿勢は4種類の全身回転です。手足を個別に広げる大の字は専用ポーズとして未実装です。
- 徘徊は既存ナビゲーションと同じ平面移動です。階段の昇降や別階への移動は行いません。到達可能な候補がない場合は停止して再探索します。
- GAME OVER 中の通常敵は初期のスロー演出後に停止します。背景で移動するのは敵化した演出個体です。
- 被弾方向は通常ステージの近接攻撃・投射物・爆発を記録します。方向を持たない直接 ApplyDamage（デバッグ操作等）は正面向きを基準に倒れます。

## 変更ファイル

- 新規: `Game/GameOverSequence.h`, `.cpp`（状態、死亡情報、仮モデル、カメラ、演出AI）、`Game/GameOverOverlay.h`（メニュー）、`Game/FirstPersonCamera.h`（カメラ球の衝突）
- 一人称の近距離描画: `Game/enemy/Enemy.h`, `.cpp`（カメラ近傍のメッシュ・破片を演出中だけ描画抑制）
- 統合: `Game/scene/Main/GameScene.h`, `.cpp`（死亡入口、描画、入力遮断、Retry / Title、終了処理）
- 流用機能の拡張: `Game/StartPlayerVisual.h`（全身姿勢と接地）、`Game/player/Player.h`, `.cpp`（被弾方向）、`Game/StageProjectile.h`（命中方向の出力）
- 検証: `tests/GameOverSequenceRuntimeTests.h`, `tools/test-gameover-runtime.ps1`, `Engine/Core/GameApp.cpp`, `Engine/Input/Input.h`
- 既存の一時遷移チェッカーの追従: `Game/TemporarySceneTransitionCheck.h`, `tests/TemporarySceneTransitionCheckTests.h`
- 既存の開始カメラ回帰テスト: `tests/CameraSequenceRuntimeTests.h`（保存済みカメラ時間に追従。旧4秒固定の前提を除去）
- プロジェクト登録: `CG2_Setup.vcxproj`, `.filters`

## 実行テスト

```powershell
./tools/build.ps1 -Configuration Debug
./tools/build.ps1 -Configuration Release
./tools/test-gameover-runtime.ps1 -Configuration Debug -Stage stage03
./tools/test-gameover-runtime.ps1 -Configuration Release -Stage stage03
```

stage01 / stage02 も `-Stage` で指定できます。各結果は `generated/gameover-tests/<stage>/result-<Configuration>.txt`、段階ごとの D3D12 描画キャプチャは同じディレクトリの Debug / Release 以下に保存されます。
死亡情報、同一モデルの継続、入力保持、20秒間の徘徊と経路衝突、敵数と進行の独立性、Retry / Title のフェード、途中のステージ再読み込み、30 / 60 / 144 FPS、4姿勢×4方向、無効dt、繰り返しResetを検証します。
一人称カメラの追加検証では、移動・回転した座標系で壁際、部屋の隅、狭い廊下、低い天井と専用ゲート、初期の壁との重なり、空中死亡を再現します。各区間の衝突・高さ・視線方向・連続性、30 / 60 / 144 FPS と長いフレームの一致、垂直付近の視線、自身のモデルの非表示を検証します。
意識演出では、倒れ込み・床での待機中に視界が全開であること、閉眼が徐々に減速すること、全黒の実画素、開眼の単調性、全開後のまばたき禁止、床目線、頭を動かした後の低い位置での静止、停止開始・長さの秒数指定、カスタム時間・カーブ、全状態でのリセットを検証します。1280×720、853×479、1920×1080、600×900 の実際のGPU描画領域で、閉眼時の全画素、開眼時の中央の隙間と周辺の黒、全開時にまぶたが残らないこと、透過暗転が画面全体を覆うことを確認します。
検証スクリプトはDebugとReleaseで実行中の一時出力を共有するため、同じステージの検証を同時実行しないでください。完了後の結果と画像は構成別に保存します。
隠しウィンドウで実行するため、メニュー遷移先はUIと共通の LeaveGameOver を直接呼び出して検証します。物理的なキーボード・マウスによる操作確認とは区別してください。

### 現在の初期設定の時間表

| 状態 | 死亡からの時刻 | 長さ |
| --- | --- | --- |
| Death（立った硬直） | 0.00〜0.30秒 | 0.30秒 |
| Collapse | 0.30〜1.50秒 | 1.20秒 |
| FloorHold（接地・倒れたまま見る） | 1.50〜1.80秒 | 0.30秒 |
| Closing | 1.80〜2.50秒 | 0.70秒 |
| Corpse（完全な暗闇） | 2.50〜3.70秒 | 1.20秒 |
| Opening（1回の開眼） | 3.70〜5.50秒 | 1.80秒 |
| Awake（全開で静止） | 5.50〜6.10秒 | 0.60秒 |
| Rising | 6.10〜9.30秒 | 3.20秒 |
| Rising内の一時停止 | 6.65〜7.10秒 | 0.45秒（Rising開始0.55秒後） |
| Possess | 9.30〜10.40秒 | 1.10秒 |
| Wander / GAME OVER | 10.40秒〜 | 従来どおりWander開始後に表示 |

### 硬直・接地・閉眼短縮の検証（2026-10-23）

| 確認 | Debug | Release |
| --- | --- | --- |
| x64ビルド | PASS（警告0・エラー0） | PASS（警告0・エラー0） |
| stage01 全状態・描画・徘徊・Retry / Title | PASS | PASS |
| stage02 全状態・描画・徘徊・Retry / Title | PASS | PASS |
| stage03 全状態・描画・徘徊・Retry / Title | PASS | PASS |

立った死亡姿勢、わずかな視点低下と揺れ、硬直から倒れ込みへの連続性、倒れ込み初期・中盤・接地直前の移動速度、接地の小さな揺れと収束を検証しました。床待機を0.03秒へ短縮して接地の揺れが閉眼区間へまたぐ場合も、境界で途切れないことを確認しています。
30 / 60 / 144 FPSで全区間の状態・カメラ・暗さ・開き具合を比較し、区間をまたぐ長いフレームでも一致しました。24条件の衝突、4姿勢×4方向、4描画サイズのGPUピクセル検証、保持入力遮断、HP・武器・敵数・進行時計の保全、20秒間の徘徊、Retry / Title、途中終了・Resetも通過しています。

今回の段階画像は `01-stagger`、`02-fall-start`、`03-falling`、`04-landing`、`05-floor-visible`、`06-closing`、`07-darkness`、`08-opening-early`、`09-opening-late`、`10-eyes-open-still`、`11-head-movement`、`12-head-pause`、`13-rising`、`14-finish-rise`、`15-possess`、`16-game-over`、`17-wander`、`18-retry`、`19-title` の順です。立った硬直、倒れ始め、接地のD3D12キャプチャを目視確認しました。
実キー／マウス操作は未実施です。Retry / TitleはUI共通ハンドラーによる自動確認です。

### 以前のテンポ調整の検証（2026-10-23、硬直追加前）

- Debug / Release x64ビルド: 警告0・エラー0。
- 全3ステージ、Debug / Releaseで、視界維持→減速する閉眼→完全な暗闇→一度だけ開眼→静止→頭・停止・身体の起き上がり→Possess→Wander→Retry / Titleを自動検証。
- 30 / 60 / 144 FPSで、状態・カメラ・暗さ・開き具合を比較。区間境界の位置・回転・暗さの連続性、閉眼の減速、開眼の単調性、全開後のまばたき禁止を検証。
- 24条件の床・壁際・隅・廊下・天井・ゲート・空中死亡、4姿勢×4方向、4描画サイズのGPUピクセル検証、保持入力、20秒間のWander、設定変更、途中のシーン終了・Resetも確認。
- 静止開始を頭の動きの終了前へ移動する設定と、静止時間0秒でも補間区間が消えないようにし、カメラが飛ばないことを検証。

現在の段階画像は `01-death`、`02-collapse`、`03-floor-visible`、`04-slow-closing`、`05-darkness`、`06-opening-early`、`07-opening-late`、`08-eyes-open-still`、`09-head-movement`、`10-head-pause`、`11-rising`、`12-finish-rise`、`13-possess`、`14-game-over`、`15-wander`、`16-retry`、`17-title` です。古い命名の画像が残っている場合はこの一覧で識別してください。
倒れた直後の全開の視界、閉眼中、一度だけ開いていく視界、起き上がりをD3D12キャプチャで確認しました。実キー／マウス操作は未実施です。Retry / TitleはUI共通ハンドラーによる自動確認です。

### 以前の意識喪失・開眼の追加検証（2026-10-23、テンポ調整前）

| 確認 | Debug | Release |
| --- | --- | --- |
| x64 ビルド | PASS（警告0・エラー0） | PASS（警告0・エラー0） |
| stage01 全状態・描画・Wander・Retry・Title | PASS | PASS |
| stage02 全状態・描画・Wander・Retry・Title | PASS | PASS |
| stage03 全状態・描画・Wander・Retry・Title | PASS | PASS |
| 既存 ScreenTransition GPU 回帰テスト | PASS | PASS |
| 一時遷移チェッカー（死亡・Retry含む） | — | PASS |

各ステージで、意識喪失中に画面全体がRGBA=(0,0,0,255)であること、少し開いて閉じ直す動き、全開後の待機、起き上がり中の静止、30 / 60 / 144 FPSと長いフレームの一致、24条件のカメラ衝突、4種類のGPU描画サイズ、保持入力遮断、開眼途中のシーン終了とリセット、設定変更、20秒間の自動徘徊を検証しました。武器・HP・敵数・ステージ時計の停止条件も通過しています。

段階画像は `01-death`、`02-collapse`、`03-unconscious`、`04-peek`、`05-blink`、`06-eyes-open`、`07-head-movement`、`08-rising`、`09-rise-pause`、`10-finish-rise`、`11-possess`、`12-game-over`、`13-wander`、`14-retry`、`15-title` の順で保存します。4解像度の全黒／隙間／全開／暗転検証は `vision-<幅>x<高さ>-<0〜3>.png` です。
開眼・閉じ直し・全開・起き上がりのD3D12キャプチャを目視確認しました。実キー／マウス操作は未実施で、Retry / Title はUIと共通のコマンドハンドラーによる自動確認です。

### 以前の一人称化の確認結果（2026-10-23、意識演出追加前）

旧版は Collapse で死亡体の外側へ移動し、壁で短縮されたカメラ距離によって全身モデルの表示を切り替えていました。そのため、死亡位置により第三者視点とモデル内の視点が混在しました。外側への移動と距離による自身の表示を廃止しました。また、世界 Z 軸まわりの傾きが死亡時の方角によって視線を曲げていたため、ローカル前方軸まわりの回転へ変更しました。

| 確認 | Debug | Release |
| --- | --- | --- |
| x64 ビルド | PASS（警告0・エラー0） | PASS（警告0・エラー0） |
| stage01 一人称演出・徘徊・Retry・Title・再読み込み | PASS | PASS |
| stage02 一人称演出・徘徊・Retry・Title・再読み込み | PASS | PASS |
| stage03 一人称演出・徘徊・Retry・Title・再読み込み | PASS | PASS |

各実行に、6条件×4方向の24ケース、回転・非一様拡大された壁への長距離球スイープ、床付近の保持と起立後の高さ、境界の連続性、ほぼ垂直の視線、初期重なり解消、4死亡姿勢×4方向、30 / 144 FPSと複数区間をまたぐ長いフレーム、起立後の揺れの減衰を含みます。
従来の20秒間の衝突付き徘徊、敵数・撃破数・進行時計の保全、保持入力の遮断、Retry / Title・途中再読み込みも通過しています。区間時計は倍精度で保持します。

段階ごとの D3D12 キャプチャで、床付近の一人称視点から起立・GAME OVERへつながり、自身の全身モデルが表示されないことを確認しました。実キー／マウス操作は未実施です。Retry / Title はUIと共通のコマンドハンドラーを自動実行しています。

### 以前の確認結果（2026-10-22、第三者視点版）

| 確認 | Debug | Release |
| --- | --- | --- |
| x64 ビルド | PASS（警告0・エラー0） | PASS（警告0・エラー0） |
| stage01 死亡・徘徊・Retry・Title・再読み込み | PASS | PASS |
| stage02 死亡・徘徊・Retry・Title・再読み込み | PASS | PASS |
| stage03 死亡・徘徊・Retry・Title・再読み込み | PASS | PASS |
| 既存 ScreenTransition GPU テスト | PASS | PASS |
| 既存開始カメラ（stage03、保存済み4.35秒） | PASS | PASS |
| 一時遷移チェッカー（Title開始・クリア・死亡・Retry） | — | PASS |

各ゲームオーバーテストには、4姿勢×4方向、30 / 144 FPS比較、敵数・撃破数・ステージ時計の保全、保持入力遮断、20秒間の衝突付き徘徊、倒れ込み中の再読み込みも含みます。
死亡体・起き上がり・メニューのGPU画像を確認済みです。実キー／マウスによる手動操作と、開始カメラのフォーカス必須のSpace分岐は未検証です（スキップ評価器と入力保持ガードは自動検証済み）。
