# Stage03 最終脱出シャッター

2026-10-16。既存のホテルと未コミット変更を保持し、最終出口のシャッター単体を実装。
作業開始時の変更は `generated/shutter-before-20261016/` にバックアップした。

## 動作と操作

Stage03を開始すると閉鎖。1階宴会ホール西側の既存ボタン **(12.25, 1.25, 21)** に近づき、
画面中央下の **「E：シャッターを開く」** が出たらEを押す。操作距離2mはプレイヤーの足元+Y=1.25からの3次元距離。
初期開放時間30秒、一定速度約0.106667m/sで上昇し、全開を維持する。再操作で停止・閉鎖しない。
通行可能になった直後にGoalへ到達しても、開放は全開まで継続する。
近くに武器があっても未操作スイッチがEを優先し、同じEでの拾得を防ぐ。左クリックの射撃経路は独立。
操作済みスイッチの付近では次のEで通常の武器拾得ができる。

ランプは操作前=緑、開放中=橙点滅、全開=青。操作後の案内も「開放中／全開（操作済み）」へ変更。
Restartはシーン再生成によってClosed、進行率0、閉鎖Collider、緑ランプへ戻る。
DebugではESCでマウスを解放し、Spawn Systemの **Teleport to shutter switch** で確認場所へ移動可能。
開放中のF1デバッグ停止／コマ戻しにも状態・Collider・描画が追従する。

## 状態・描画・衝突

`StageShutterSystem` は入力・描画・音・敵処理に依存しない状態管理。
通常の遷移は **Closed → Opening → Open** の一方向。
`elapsedTime` はdoubleで加算し、`progress=clamp(elapsedTime/openDuration,0,1)` を計算する。
位置は `closedPosition + (0, openDistance * progress, 0)`。
Eの操作が成立したフレーム以前のdtは加算せず、次の更新から上昇する。

既存 `Hotel_Shutter_Raised_Placeholder` のメッシュ・材質・X/Zを流用し、高さを開口に合わせ3.2mへ変更。
閉鎖時の範囲はX=11.775～12.225、Y=0～3.2、Z=16～20。メッシュの原点は下端中心 **(12,0,18)**。
その他のステージ形状、301～308、開始位置(34,12,19)、宴会ホール、6 Spawn、防火扉、屋外Goalは保持。

ExporterはShutterを静的なstage03.gltfと静的Collider配列から除き、`shutter_000.gltf/.bin` へ個別出力する。
JSON内の専用ColliderをStageLoaderが通常のStageWorld配列へ1個追加する。
各更新で同じ下端位置をObject3dとColliderに適用し、Colliderのworldとinverseを両方再生成。
その後にプレイヤー／敵移動、hitscan、プレイヤー弾、敵弾／爆弾が同じStageWorldを参照する。
通行可否のタイマー切替や全開時のCollider無効化は行わず、現在の実開口高さで判断する。

## データ編集

`resources/levels/stage03/stage03.json` の `shutters[0]`：

|項目|初期値|用途|
|---|---|---|
|openDuration|30|開放時間（秒、正数）|
|openDistance|3.2|上昇距離（m、正数）|
|closedPosition|[12,0,18]|閉鎖時の下端中心|
|buttonPosition|[12.25,1.25,21]|操作判定・ランプ基準|
|interactionDistance|2|操作半径（m、正数）|
|collider|下端からの相対TRSとlocalBounds|動的OBB|
|progressEvents|0.5 / halfOpened|一度だけ発火する途中イベント|
|sounds|4つのWAVパス|switch / start / motor / complete|

JSON変更はRestartで反映。再Exportで上書きされるため恒久編集はBlenderを推奨。
Blenderで既存シャッターを選択し、YanEngine LevelのObject Type **Shutter** を使用。
Switch Object、Open Duration、Open Distance、Interaction Distance、Progress Events (JSON)、各WAVパスを編集してExport。
閉鎖位置・形状はメッシュの位置／寸法から算出し、ボタンの位置も指定オブジェクトから取得する。
例：`[{"progress":0.5,"event":"halfOpened"}]`。途中イベントの進行率は0より大きく1未満。
音は通常のPCM WAVを用意する。現在は4ファイルとも未用意で、存在しない場合は読み込みを呼ばず無音動作。
motorのみループし、全開・デバッグ停止・シーン終了で停止する。

## 次段階の敵イベント連携

`StageShutterSystem::TakeEvents()` はシャッターID・イベント種別・名前・進行率を返す。
Started（openingStarted）、設定した進行率のProgress、Completed（fullyOpened）が各一度だけ届く。
長いフレームで複数の進行率を跨いでも順番に通知し、Restartで再通知可能に戻る。
`GameScene::SetShutterEventHandler()` がゲーム側の連携口。GameSceneは音の処理後にこのハンドラへ通知する。

既存SpawnGroupのSimultaneousは同じUpdateで全メンバーを出現させ、他グループの生存数を待たない。
既存SpawnTriggerは入場検知とmaxAliveを持つ別の仕組み。
次段階ではイベント名をSpawnGroupの開始へ接続する処理を追加できる。
今回は敵設定・グループ・トリガーを追加せず、既存の6地点は待機状態を維持。
シャッターは敵数やグループの終了を一切参照しない。

## 変更ファイル

- `Game/StageShutter.h`：状態、下端位置、進行率イベント、操作判定、Collider同期。
- `Game/StageLoader.h`、`Game/StageWorld.h`：データ検証、動的モデルの依存ファイル確認、行列更新。
- `Game/scene/Main/GameScene.h/.cpp`：E優先順位、描画、ランプ、任意音声、イベント、Debug履歴。
- `Game/GameHUD.h/.cpp`：スイッチが優先される間は武器拾得案内を抑制。
- `tools/blender/yanengine_level_exporter.py`：Shutter役割、編集UI、個別glTF出力。
- `tools/blender/configure_stage03_shutter.py`：既存.blendのシャッターだけを変換する一度限りの移行用。
- `tools/blender/create_stage03.py`：将来の全体再生成にも同じシャッター設定を適用。
- `resources/levels/stage03/stage03.blend/.json/.gltf/.bin`、`shutter_000.gltf/.bin`：既存モデルの置換。
- `resources/ui/shutter_prompt.png`、`tools/create-shutter-ui.ps1`：日本語案内と再生成スクリプト。
- `tests/ShutterChecks.h`、`tests/StageLoaderTests.cpp`、`tests/HotelStageChecks.h`、`tests/HotelStageRuntimeTests.h`：検証。
- `tools/test-hotel-runtime.ps1`：GPU起動・複数シーン検証のタイムアウトを600秒に調整。
- `CG2_Setup.vcxproj/.filters`：ヘッダー登録とReleaseのPDB問題回避。Releaseはデバッグ情報生成を無効化し、Debugは既存の/Z7を維持。

## 検証手順

```powershell
tools/test-stage-loader.ps1
tools/test-hotel-stage.ps1
tools/build.ps1 -Configuration Debug
tools/test-hotel-runtime.ps1 -Configuration Debug
tools/build.ps1 -Configuration Release
tools/test-hotel-runtime.ps1 -Configuration Release
```

Blenderは `--background --factory-startup --python-exit-code 1 --python tools/blender/test_stage03.py`。
既存Exporterの回帰は同じ引数で `test_level_exporter.py`。
ビルド完了後にランタイムテストを起動する（実行中の同じEXEはリンクで上書きできない）。

自動検証は30/60/144 FPSでの30秒開放・線形速度・毎フレームのCollider行列、操作範囲、再操作、
再起動、イベント一回性、身長差の通行可否、開口内外のray、敵弾遮断、静的モデルとの非重複、
メッシュとColliderの6面一致、不正JSONのロールバックを含む。
CPUホテルテストは全8室と全経路を各FPSで往復。GPUテストは実シーンのEハンドラ、武器との競合、
Object3dのGPU行列、閉鎖／15秒／全開の画像、全経路・Goal、Restart、Stage01／02／Debug Showroomを確認する。
GPUテストではCPUテストと重複する全経路の3FPS往復だけを省略する。

結果と画像は `generated/hotel-tests/Debug/`、`Release/`。
Stage02／Showroomは同じスクリプトから別プロセスで検証し、`regression-Debug.txt`／`regression-Release.txt` に記録する。
既存のSRV管理は最大512個の単調確保で、大量のシーン連続切替では上限に達する。
今回の検証では重複する初回ロードを除き、回帰検証のプロセスを分離した。SRV管理自体の改修は行っていない。

## 未実装

敵グループの具体的なイベント割り当て、敵の出現設定、正式な音声ファイル、完成版のシャッター装飾・格納演出。
今回の仮モデルは板を上方へ移動させる方式。

## 今回の検証結果

- Debug / Releaseビルド：成功（最終ビルドは警告・エラーなし）。
- StageLoaderTests：成功。既存ステージとシャッターJSONの不正値・ロールバック・依存モデル検証。
- HotelStageTests：成功。30/60/144 FPSで30秒開放と衝突、全8室とホテル全経路の往復。
- Debug / Releaseランタイム：両方成功。E処理、重複する拾得対象の優先順位、毎フレームのObject3d/GPU行列、
  途中Goalでも全開継続、RestartのClosed復帰、Stage01とStage02、Debug Showroom。
- Blender保存モデルとJSONの一致、編集後の再Export、シャッター個別モデルの保持：成功。
- 既存Blender Level Exporterの回帰：成功。Blender終了時に既存の微小な未解放メモリ警告が出るが、テスト終了コード0。
- 閉鎖／15秒／全開の実ゲーム画像をDebug / Releaseで保存して目視確認。日本語案内・板の下端・ランプ表示を確認。
  OSの物理Eキーによる手動検証は行っていない。E入力から呼ばれる実ハンドラはランタイムテストで検証済み。
  ウィンドウ操作はユーザーのEscで停止し、以降は自動検証と保存画像で確認した。
- 作業開始時のJSONと比較し、シャッター以外の全項目が一致。今回触れていない既存変更ファイルのSHA256も一致。

証跡：`generated/hotel-tests/result-Debug.txt`、`result-Release.txt`、
`regression-Debug.txt`、`regression-Release.txt`、各構成の画像フォルダ。
