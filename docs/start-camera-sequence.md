# 開始カメラ演出（CameraSequence）

## 実装と責務

- `Game/CameraSequence.h`: 描画・入力から独立したシーケンス定義、JSON検証、時間管理、任意時刻の評価、停止、スキップ。位置は区間ごとにCatmullRom / Linear / SmoothStep / Holdを選択。回転は既存Quaternionの最短経路Slerpを使用する。
- `Game/StageLoader.h`: 任意フィールド`cameraSequences`を読み込む。存在しない場合は従来動作。不正なカメラ設定は`cameraWarning`へ記録し、ステージ本体を有効なまま演出を省略する。ステージ本体の読み込み失敗・ロールバック処理は既存どおり。
- `Game/scene/Main/GameScene.{h,cpp}`: `start`シーケンスへのCamera所有権切り替え、フェード待機、ゲーム停止、HUD抑制、FPS復帰、ImGui操作を担当する。新しいCameraやフェードシステムは作成していない。
- `Game/player/Player.h`: `EyePosition()`で通常FPSカメラと同じ目の位置を公開する。
- `Game/StartPlayerVisual.h`: Normalの既存EnemyAssetの三角形から描画専用モデルを作る。敵AI・HP・EnemyPoolは使用しない。身長を目の高さ+0.18mへ合わせ、足元とYawをPlayerへ追従させる。正式モデルへの交換場所はこのクラス。
- `tools/blender/yanengine_level_exporter.py`: Camera Point、設定パネル、経路・ラベル表示、Preview、Export、Validate。インストールするファイルは引き続きこの1ファイル。
- Stage01 / Stage02 / Stage03のJSONと.blendに7点・約4秒の初期経路を追加。Stage03はRoom308の浴室壁を避ける狭い経路に調整した。既存のメッシュ、当たり判定、敵・武器・ゴール設定は変更していない。

開始演出以外も、別のSequence IDと別のCameraSequenceインスタンスで評価できる。今回は他イベントをゲーム進行へ接続していない。

## Blenderでの編集

1. 更新した`yanengine_level_exporter.py`をInstall from Diskで再インストール・有効化する。既存.blendはBlender 5.2で編集・検証済み。
2. Nパネル → **YanEngine Level → Camera Sequences**を開く。
3. 既存ステージでは`StartCamera_00`～`06`を選択する。新規ステージではPlayer Spawnを1つ配置し、**4s Start Path**で初期経路を作る。既存のstart経路は上書きしない。
4. 個別追加は**Add Camera Point**。通常のEmptyを作ってObject Typeを**Camera Point**へ変更してもよい。
5. 通常のG / Rで位置・回転を編集する。Manualの前方はEmptyのローカル **-Y**、上方向は **+Z**。Emptyの軸表示に加え、実際の評価結果の前方をオレンジ線で表示する。
6. Point IDは通常のID欄（空欄ならオブジェクト名）。Sequence IDを`start`にし、Orderを重複しない整数にする。順番の変更はOrderを書き換える。
7. **Duration to next**は、その点から次の点までの秒数。最後の点だけ0にする。全体時間は各区間の合計。
8. Rotation ModeはManual / Look At Player / Look At Target。後者は同じシーン内のターゲットオブジェクトを指定する。Look At Player heightで胴体・頭の注視高さを調整できる。
9. 最後の点は**PlayerEye**を有効にする。この点のEmpty座標・回転・FOVは実行時には使用せず、実際のFPS目位置・向き・FOVに置き換える。
10. **Validate** → **Export**。従来のステージJSONへcameraSequencesが追加される。カメラEmptyやプレビューカメラはステージglTFへ出力しない。

`Enable Start Sequence`でステージ単位の有効・無効を切り替える。Camera Pointのない旧ステージにはcameraSequencesフィールドを追加しない。

シアンの線は実際の補間曲線のサンプル、オレンジは各点のカメラ前方。ラベルにはSequence ID、Order、Point ID、開始秒、次区間の長さ、全体時間を表示する。`Show Camera Paths`で隠せる。

Holdは次の点と同じ位置・回転設定・注視設定・FOVの場合に限る。停止時間を追加するときは同じポーズを2点作り、前の点をHoldにする。曲線は制御点の外側へ膨らむ場合があるため、壁際ではLinear / SmoothStepへ切り替えるか、点を追加して実際の経路を確認する。

## Blenderプレビュー

- Preview Sequenceで対象IDを選び、**Preview / Restart**を押す。現在の3Dビューポートだけを一時カメラ視点へ切り替え、0秒から再生する。
- **Preview time**へ任意の秒数を入力するとその時刻へ移動する。その位置から再生を続ける。最後は最終ポーズで停止表示する。
- **Stop**またはEscで、開始前の編集視点・ローカルカメラ設定へ戻す。再生中のPreview / Restartは0秒へ戻す。
- プレビューは一時Cameraとタイマーだけを作る。シーンの本来のCamera、タイムライン、キーフレーム、ステージメッシュは変更しない。Stop、保存前、別ファイル読み込み前、アドオン無効化時に一時データを破棄する。
- Preview FPS eye height / FOVの初期値はゲーム標準の1.6m / 垂直60度。ゲームのPlayer設定を変更した場合はプレビュー値も合わせる。

ゲーム側と同じエンジン座標`(-Blender X, Blender Z, -Blender Y)`、XYZ回転、区間時間、Catmull-Rom式、動的注視、Quaternion Slerpを使う。Exportとプレビューは同じ定義作成関数を使う。Look At TargetはExport時のワールド座標を保存する。

## JSON

ルートの任意配列。最小の例:

```json
"cameraSequences": [
  {
    "id": "start",
    "enabled": true,
    "points": [
      {
        "id": "front",
        "order": 0,
        "position": [5.8, 1.8, -2.5],
        "rotation": [0, 0, 0],
        "duration": 3.5,
        "interpolation": "SmoothStep",
        "rotationMode": "LookAtPlayer",
        "lookAtOffset": [0, 1, 0],
        "fov": 60,
        "playerEye": false
      },
      {
        "id": "eye",
        "order": 1,
        "duration": 0,
        "interpolation": "Linear",
        "rotationMode": "Manual",
        "playerEye": true
      }
    ]
  }
]
```

位置はエンジンのワールド座標、回転はXYZラジアン、fovは垂直方向の度数（10～150）。Camera APIへ渡す直前にラジアンへ変換する。LookAtTargetでは`lookAtTarget: [x,y,z]`が必要。PlayerEyeのFOVは常に通常FPS値を使用する。

制限: 最大128シーケンス、各2～1024点、OrderとIDの重複禁止、非終端のDurationは0.001～1200秒、終端は0秒、全体3600秒以下。startは必ずPlayerEyeで終わる。未知の補間・回転モード、NaN / Infinity、欠けたベクトルなどを拒否する。

初期経路の区間: 0～0.5秒は斜め前で全身を保持、0.5～2.5秒は縮小しながら回り込み、2.5～3.5秒で背後から目の高さへ、3.5～4秒で目へ入る。最後の位置・回転・FOVは実行時Playerから取得する。

## ゲーム内動作

既存のタイトル破壊 → FadeOut → ステージ読み込みの順序は維持する。OnEnterで開始カメラと各モデルのWVPを用意してからFadeInを描画する。FadeIn / Hold / ロード中は時間0で保持し、遷移完了後の最初の更新もロード時間を破棄する。

演出中はPlayer::Update / UpdateCombat / Spawnトリガー / 武器タイマー / ステージ時計 / シャッター進行を呼ばず、描画用の更新のみ続ける。開始時刻0のSpawnGroupは既存どおり読み込み中に準備するが、演出中にAIや出現時刻は進まない。通常HUDとクロスヘアを隠す。カメラが目から0.85m以内へ入る前に仮プレイヤーモデルを隠す。

自然終了・Spaceスキップ・デバッグ停止は共通のFinishStartSequenceを通す。Player自身で通常カメラを復元し、FOVを一致させる。終了フレームにはゲーム入力を消費しない。押しっぱなしのSpace / 左右マウス / R / Eはリリースまで入力だけを抑制し、ゲーム進行は再開する。終了処理は複数回呼んでも副作用を重ねない。

Restart Stageは通常のシーン再生成で最初から再生する。Showroomと配置テストでは再生しない。既存の`-test` / `-smoke`起動では従来のゲーム時間を維持するため自動省略し、専用`--camera-sequence-test`だけ有効にする。通常起動でも`--no-start-camera`で省略可能。プログラムからは`SetStartSequenceEnabled(false)`をOnEnter前に呼べる。

Debugの既存**FPS Controls → Start Camera Sequence**にEnable / Play / Stop / Restart、経過時間、現在Point ID、座標、回転、FOVを追加した。再生・停止要求は次のUpdateで処理し、DrawImGui中にカメラを切り替えない。ステージ設定がdisabledでもデバッグPlayで確認できる（Enableチェックが必要）。

## 検証コマンドと生成物

プロジェクトルートからPowerShellで実行:

```powershell
./tools/build.ps1 -Configuration Debug
./tools/build.ps1 -Configuration Release
./tools/test-blender-camera.ps1
./tools/test-camera-sequence.ps1
./tools/test-camera-runtime.ps1 -Configuration Debug -Stage stage01
./tools/test-camera-runtime.ps1 -Configuration Release -Stage stage03
```

専用単体テストは30 / 60 / 144FPS、巨大・不正dt、保持、回転連続性、Manual / 動的注視 / Slerp、スキップ、再開始、異なるSpawn / FOVへのPlayerEye、disabled / 旧JSON / 不正カメラのフォールバックを確認する。全3ステージで1000区間のカメラ経路を半径0.12mで既存コライダーへSweepする。

Blenderテストはステージから生成した各241時刻とManual / LookAtTarget混合161時刻の参照を出力し、C++の結果と位置・回転行列・FOVを比較する。追加・設定・Validate・Export・プレビューの任意時刻・Stop / Restart / 編集視点復元・別名保存 / 再読込・登録解除も検証する。元の.blendをテスト中に保存しない。

実行時テストはD3D12で0 / 0.5 / 1.5 / 2.5 / 3.5 / 3.9 / 4秒の画像を保存し、FadeIn中の保持、時計・AI・武器・Poolの停止、FPS引き継ぎ、入力持ち越し防止、ゲーム再開、Restartを確認する。出力は`generated/camera-tests/<stage>/Debug/`および`Release/`、結果は同じ階層の`result-Debug.txt` / `result-Release.txt`。

`tools/blender/configure_start_cameras.py`は同梱ステージの初期経路を追加した移行用スクリプト。通常の編集では実行不要。

## 変更ファイル一覧

| 対象 | ファイル |
| --- | --- |
| 新規ランタイム | `Game/CameraSequence.h`, `Game/StartPlayerVisual.h` |
| ゲーム接続 | `Game/StageLoader.h`, `Game/player/Player.h`, `Game/scene/Main/GameScene.h`, `Game/scene/Main/GameScene.cpp` |
| 実行テスト接続 | `Engine/Core/GameApp.cpp`（専用テスト起動）, `Engine/Input/Input.h`（テスト用friend） |
| Visual Studio登録 | `CG2_Setup.vcxproj`, `CG2_Setup.vcxproj.filters` |
| Blender | `tools/blender/yanengine_level_exporter.py`, `tools/blender/configure_start_cameras.py` |
| ステージ | `resources/levels/stage01/stage01.json`と`.blend`、`stage02/stage02.json`と`.blend`、`stage03/stage03.json`と`.blend` |
| 新規テスト | `tests/CameraSequenceTests.cpp`, `tests/CameraSequenceRuntimeTests.h`, `tools/blender/test_camera_sequences.py` |
| 新規テスト実行 | `tools/test-camera-sequence.ps1`, `tools/test-camera-runtime.ps1`, `tools/test-blender-camera.ps1` |
| 既存テスト調整 | `tests/ScreenTransitionTests.h`（Releaseの既存プロファイルでは非表示のDebugパネルを要求しない）, `tools/blender/test_level_addon_registration.py`（アドオン版番号）, `tools/test-stage-progress.ps1`（不足していたMatrix4x4のリンク依存を追加） |
| 説明書 | `docs/start-camera-sequence.md` |

## 今回の検証結果（2026-10-20）

- Debug / Releaseビルド成功。両方とも警告0・エラー0。
- 全3ステージ × Debug / Releaseの6実行で専用D3D12テストがPASS。開始・回り込み・背後・FPSの保存画像も確認した。
- C++カメラ単体テストPASS。30 / 60 / 144FPS、衝突Sweep、変更したSpawn / FOV、異常値、無効・旧JSONを含む。
- Blender 5.2の新規統合テストとC++との比較がPASS。従来のLevel Export / 武器カタログ / 登録・再有効化テストもPASS。
- 既存のStageLoader、FPSMotion、Showroom、Weapon、BulletテストPASS。実D3D12のTitleScene、EnemyPoolテストPASS。
- 既存のScreenTransition GPUテストはDebug / ReleaseともPASS。ステージクリア・タイトル復帰・フェード・初回WVPを含む。
- StageProgressの実行スクリプトは既存の行列リンク依存が不足していたため補正し、再検証した。

検証ログ・画像は`generated/camera-*`および既存テストの`generated/*-tests/`に保存している。

## 調整と範囲

調整可能: 点の位置・回転・順序、区間秒数、補間、注視モード・ターゲット・高さ、FOV、ステージ有効状態、プレビューEye高さ。仮モデルの身長・モデル差し替え・非表示距離はStartPlayerVisualとGameSceneで変更できる。

HUDの短いフェードインとゲーム内の経路線表示は任意要件として未実装（HUDは終了時に即表示、経路はBlenderで表示）。自動的なカメラ衝突回避は追加しておらず、編集した経路の壁・天井との関係はプレビューとゲームで確認する。保存済み初期経路にはSweepテストを実施済み。

バックグラウンド検証ではBlenderのビューポート状態・カメラ行列とオペレーターを検証している。人が操作するUI上のタイマー連続再生と経路ラベルの見た目は未検証。非フォーカスの非表示ゲームウィンドウでは実キーボードのSpace分岐を実行できない場合があり、結果ファイルにその旨を記録する（スキップ本体と入力ガードは検証）。各ステージの全ルートを人が通して遊ぶ確認は未実施。
