# YanEngine デザインパターン調査

基準はブランチ `クラス設計改善` の `27c749d`。開始時の作業ツリーに未コミット変更はなかった。現行ソリューション、`main.cpp`、`Game/` と `Engine/` の追跡対象288ファイルを範囲とし、ディレクトリ構成、状態の書き込み、仮想関数、所有権、通常の呼び出し元を調べた。関連する既存テスト、配置・Blenderエクスポート処理も照合した。依存ライブラリと `archive/` の旧版は現行ゲームの実績から外した。

## 初回調査

| 対象機能 | 現在の実装方式 | 該当パターン | 判定 | 改善案 |
| --- | --- | --- | --- | --- |
| **ゲーム／敵AI** | [EnemyAI.h](../Game/enemy/EnemyAI.h) の `EnemyAI::Update` が認識・追跡・調査・捜索・徘徊を分岐。[Enemy.cpp](../Game/enemy/Enemy.cpp) の `Enemy::Update` から呼ばれる。 | 列挙型FSM | **未実装** | 優先度1。行動と遷移を状態クラスに移し、観測、値コピー、攻撃順序を維持する。攻撃は追跡と同じ更新処理を使う。 |
| **ゲーム／部位破壊・死亡** | [Enemy.cpp](../Game/enemy/Enemy.cpp) の `Enemy::ApplyDamage` と死亡処理が破片を発生させる。[EnemyParts.h](../Game/enemy/EnemyParts.h) の `EnemyPartDamageState` は損傷段階の値。 | 値による判定 | **対応不要** | 部位HPと破壊時の副作用はEnemyに集約。損傷段階ごとの状態オブジェクトは不要。 |
| **ゲーム／プレイヤー** | [Player.cpp](../Game/player/Player.cpp) の `Player::Update`、`UpdateShooting` と [FPSMotion.h](../Game/player/FPSMotion.h) が移動・ジャンプ・射撃を更新。 | 直交する入力・物理状態 | **対応不要** | 接地、ADS、射撃は同時に変化するため、排他的な行動クラスにまとめると組み合わせが増える。 |
| **ゲーム＋エンジン／アクティブシーン切替** | [IScene.h](../Engine/Core/IScene.h) を各シーンが継承。[SceneManager.cpp](../Engine/Core/SceneManager.cpp) の `UpdateScene` が `current_->Update` を呼ぶ。[GameOverScene.cpp](../Game/scene/Flow/GameOverScene.cpp) の `Update` などが遷移を要求する。 | State | **達成済み** | 遷移先は具体シーンが決め、マネージャーがライフサイクルを調整する。 |
| **エンジン／フェードと非同期のシーン切替** | [SceneManager.cpp](../Engine/Core/SceneManager.cpp) の `UpdateTransition`、`DrawOverlay2D`、[FadeManager.cpp](../Engine/Core/FadeManager.cpp) の `Update`。黒フレームの描画、ロード、復旧を順序づける。 | 列挙型FSM・描画プロトコル | **対応不要** | フレーム間の順序と復旧を一箇所で追える現構造を維持。 |
| **ゲーム／ゲームオーバー・タイトル開始** | [GameOverSequence.cpp](../Game/GameOverSequence.cpp) の `GameOverSequence::Advance`、`Update`、`UpdateCamera` と [TitleStartSequence.h](../Game/TitleStartSequence.h) の `Update`。 | 時間駆動の演出 | **対応不要** | 段階の残時間を次の段階へ引き継ぎ、カメラ・画面効果も同じ時間を使う。演出順を局所化する。 |
| **ゲーム／開始・F7カメラ、ホテル脱出演出** | [CameraSequence.h](../Game/CameraSequence.h) の `CameraSequence::Start`、`Update`、`Evaluate` が定義データを評価。[GameScene.HotelEscape.cpp](../Game/scene/Main/GameScene.HotelEscape.cpp) の `UpdateHotelEscape` がプレビュー・クリア進行を調整。 | データ駆動の補間・演出 | **対応不要** | 再生とエディターの時刻指定評価を同じ関数で扱う。ホテルのモードは同じ演出の用途を区別する。 |
| **ゲーム／シャッター・扉** | [StageShutter.h](../Game/StageShutter.h) の `StageShutterSystem::TryInteract`、`Update` と [StageDoor.h](../Game/StageDoor.h) の `StageDoorSystem::Update`。進捗・コライダー・イベントが連動。 | 小さなFSM・イベントキュー | **対応不要** | シャッターは一方向に進み、実際に継続更新する段階は開動作のみ。データ設定と衝突同期を維持する。 |
| **ゲーム／武器・投擲物** | [WeaponSystem.h](../Game/WeaponSystem.h) の `WeaponRuntime::TryFire`、`UpdateReload` と [ThrowableSystem.h](../Game/ThrowableSystem.h) の `ThrowableSystem::State`。 | 列挙型FSM・値による設定 | **対応不要** | 射撃モードは武器定義、リロードは時間と弾数の進行。バーストとリロードの干渉、弾数の不変条件はRuntimeに残す。 |
| **ゲーム／ステージ達成・各エディターのモード** | [StageProgress.h](../Game/StageProgress.h) の `StageProgress::Update` と [GameScene.cpp](../Game/scene/Main/GameScene.cpp) の `GameScene::Update` が進行フラグやEdit/Testを選択。 | 小さな値・UIルーティング | **対応不要** | 進行はゴール到達の一方向。エディターのモードは主に入力の宛先・表示を選ぶため、独立した状態クラスを導入する利点が薄い。 |
| **ゲーム／敵の取得・返却** | [EnemyPool.h](../Game/enemy/EnemyPool.h) の `EnemyPool::Initialize`、`Acquire`、`Release` が定義別スロットを所有。敵の初期化と出現時のリセットを分離。 | Object Pool | **達成済み** | 空き枠を再利用し、破片が残る敵の返却を防ぐ。枯渇時は追加確保する運用を維持。 |
| **ゲーム／敵の新規生成** | [EnemyPool.h](../Game/enemy/EnemyPool.h) の `EnemyPool::Add` は常に `Enemy` を作り、`EnemyDefinition` で構成。 | 生成ヘルパー | **対応不要** | サブクラスによる生成メソッドの上書きはない。敵タイプは定義データで増やせるためFactory Methodの導入は不要。 |
| **ゲーム／スポーン予定・生成の分担** | [EnemySpawnSystem.h](../Game/enemy/EnemySpawnSystem.h) の `EnemySpawnSystem::Update` は時刻・範囲・生存上限を処理し、[GameScene.cpp](../Game/scene/Main/GameScene.cpp) の `UpdateEnemySpawns` がコールバックでPoolから取得する。 | コールバックによる依存性逆転 | **達成済み** | Blender由来の配置・起動条件を、GPUを持たないテストからも検証できる。順次／同時は出現予定時刻の設定として扱う。 |
| **エンジン／シーン生成** | [GameApp.cpp](../Engine/Core/GameApp.cpp) の登録ラムダを [SceneManager.cpp](../Engine/Core/SceneManager.cpp) の `ChangeScene_` が呼び、`unique_ptr<IScene>` を取得。 | Factory Registry | **達成済み** | 名前から実装を生成でき、新規シーンを登録で追加できる。継承で生成処理を上書きするGoF Factory Methodとは構造が異なる。 |
| **エンジン／デバッグ行動選択** | [DebugAIManager.cpp](../Engine/DebugAI/DebugAIManager.cpp) の `SetBot` と `ProcessAfterUpdate` が [IDebugBot.h](../Engine/DebugAI/IDebugBot.h) の `ChooseAction` を使う。 | Strategy | **達成済み** | `RandomDebugBot`、`BasicCombatDebugBot`、`ApiDebugBot` を差し替えても管理側の実行・記録手順を再利用できる。 |
| **エンジン＋ゲーム／DebugAIのゲーム接続** | [IGameDebugAdapter.h](../Engine/DebugAI/IGameDebugAdapter.h) の観測・操作APIを、[DebugAITestScene.cpp](../Game/scene/Debug/DebugAITestScene.cpp) の `DebugAITestAdapter` がゲームの変数・操作に変換。 | Adapter | **達成済み** | エンジンの記録・再生コードを個別シーンの操作や内部表現から独立させる。 |
| **ゲーム／敵エディターの面割り当てUndo/Redo** | [EnemyEditor.cpp](../Game/enemy/EnemyEditor.cpp) の `ApplySelection`、`StepAssignmentHistory` が `AssignmentEdit` の差分をそれぞれ適用する。 | 差分履歴、Commandの候補 | **改善推奨** | 優先度2。既存の差分型に実行・取り消しを持たせ、新規実行とRedoで同じ処理を使う。 |
| **ゲーム／カメラ編集、ゲームの巻き戻し** | [EnemyPlacementEditorScene.Camera.cpp](../Game/scene/Debug/EnemyPlacementEditorScene.Camera.cpp) の `CommitCameraHistory`、`UndoCameraSequence` が定義全体を保存・復元。[GameScene.cpp](../Game/scene/Main/GameScene.cpp) の `CaptureDebug`、`RestoreDebug` と [DebugTimeline.h](../Game/DebugTimeline.h) も同じ役割分担。 | Memento | **達成済み** | タイマーや複数要素を含む時点を一括復元できる。履歴が状態の内容を解釈する必要がない。 |
| **ゲーム／敵エディターの輪郭とプレビュー** | [EnemyEditor.cpp](../Game/enemy/EnemyEditor.cpp) の `SetFaceSelected`、`DrawViewport` が `outlineDirty_` で再構築を遅延し、`Update` が `previewDirty_` を処理。 | Dirty Flag | **達成済み** | 操作が続いても必要な時点まで描画データの再構築をまとめる。保存有無を表す `dirty_` は別のフラグ。 |
| **ゲーム／Live Development** | [LiveLua.h](../Game/LiveDevelopment/LiveLua.h) の `LiveLua::Evaluate` と [EnemyAITestLiveDevelopment.cpp](../Game/scene/Debug/EnemyAITestLiveDevelopment.cpp)、[LiveEnemyDiagnostics.cpp](../Game/LiveDevelopment/LiveEnemyDiagnostics.cpp) の `ApplyPending` が、Lua評価と変更の一括適用・履歴を扱う。 | スクリプト駆動・変更トランザクション | **対応不要** | Luaは行動段階を選ぶクラス階層から独立した移動倍率の方策。DLL・Lua・JSONの適用順とロールバックを維持。 |
| **エンジン／モデル資源の共有** | [ModelManager.cpp](../Engine/3D/ModelManager.cpp) の `LoadModel`、`PreloadModel` は正規化キーでモデル・準備タスクを再利用。 | Resource Cache | **達成済み** | モデル読み込みとGPU生成の重複を抑える。共有モデルへの可変操作もあるため、Flyweightとしては数えない。 |
| **エンジン／スケルトンとアニメーション** | [Skeleton.cpp](../Engine/3D/Skeleton.cpp) の `CreateJointRecursive`、`UpdateSkeletonSpace` が親子インデックスを処理。[Animator.cpp](../Engine/3D/Animator.cpp) の `Update` が時刻とブレンドを進める。 | 木構造・時刻駆動 | **対応不要** | ジョイント一覧は連続データ。葉と枝の共通操作を持つGoF Compositeや、二重ディスパッチのVisitorは確認できない。 |
| **エンジン／フレーム手順・音声通知** | [GameApp.cpp](../Engine/Core/GameApp.cpp) の `GameApp::Draw` がシーンの描画段階を呼ぶ。[VideoPlayerMF.h](../Engine/3D/VideoPlayerMF.h) の `XAudio2VoiceCallback::OnBufferEnd` はXAudio2から呼ばれる。 | 委譲・登録コールバック | **対応不要** | GameApp自身の手順を継承で差し替えるTemplate Methodとは数えない。音声は外部APIの通知、シャッターは単一の消費側が取り出すキューで、独自のObserver基盤は不要。 |

優先順は敵AIと面割り当て履歴。スポーンの抽選・処理順、部位破壊、武器、演出タイミング、配置データの仕様は現行コードに従う。

## 実装結果

| 対象 | 変更後の判定 | 実装・修正内容 |
| --- | --- | --- |
| 敵AI | **達成済み** | `EnemyAI::Behavior` と6つの具体状態を追加。索敵更新を `Observe` に分け、既存の `EnemyState` から状態オブジェクトを引く。通常の追跡、調査、捜索、徘徊への遷移は状態側で決める。[EnemyAI.h](../Game/enemy/EnemyAI.h) · [EnemyAIStates.inl](../Game/enemy/EnemyAIStates.inl) |
| 敵エディター | **達成済み** | 面と割り当て先の差分を持つ `AssignmentEdit` に `Execute` と `Undo` を追加。新規適用とRedoが共通の操作を呼び、Undoは各面の以前の所属を復元する。履歴上限32件、分岐時のRedo破棄、選択・プレビュー更新はエディターで管理する。[EnemyEditor.h](../Game/enemy/EnemyEditor.h) · [EnemyEditor.cpp](../Game/enemy/EnemyEditor.cpp) |
| テストとプロジェクト | 完了 | 既存の索敵記憶テスト3か所で `PickSearchGoal` に実効移動速度を渡すよう修正。引数は初回調査時から必須だった。状態の実装ファイルをVisual Studioのプロジェクトにも登録した。[EnemyMemoryTests.cpp](../tests/EnemyMemoryTests.cpp) · [CG2_Setup.vcxproj](../CG2_Setup.vcxproj) · [CG2_Setup.vcxproj.filters](../CG2_Setup.vcxproj.filters) |

### 課題①の確認

| 条件 | 敵AI | アクティブシーン |
| --- | --- | --- |
| 状態を表すクラス | `EnemyAI::IdleState`、`PursuitState`、`InvestigateState`、`SearchState`、`WanderState`、`DeadState` | `TitleScene`、`GameScene`、`GameOverScene` など |
| 共通関数のオーバーライド | 具体状態が `Behavior::Update` を `override` | 具体シーンが `IScene::Update`、`OnEnter`、描画関数などを `override` |
| 本体から多態的に呼ぶ | `EnemyAI::Update` が `BehaviorFor` で基底参照を取得し、`Behavior::Tick` から仮想 `Update` を呼ぶ | `SceneManager::UpdateScene` と描画メソッドが `current_` の仮想関数を呼ぶ |
| 状態内が遷移を制御 | 認識による追跡優先は `Behavior::Tick`、猶予切れは `PursuitState`、到着や到達不能から捜索への移行は `InvestigateState`、経路終了から徘徊への移行は `SearchState` | 具体シーンの `Update` などが `RequestChangeScene_` や `TransitionTo` で移行先を指定し、`SceneManager` が要求を実行する |

`Chase` と `Attack` は、敵の位置・視界・間合いから毎フレーム判断する同じ追跡処理を共有する。HUDなどは引き続き両者を区別できる。観測・経路・タイマー・乱数は個別の `EnemyAI` に保存し、振る舞いオブジェクトは全個体で共有する。プール生成時の追加ヒープ割り当てが不要で、`Enemy::DebugState` の値コピーから巻き戻したフレームも現在の段階に合う処理を選べる。死亡・外部からの追跡命令は敵のライフサイクルと外部イベントとして既存の入口を使う。

### 課題②の説明要点

- **Object Pool**。再利用できる敵個体の所有者を `EnemyPool` に置く。毎回のObject3d初期化を省きつつ、部位HP・AI・姿勢は出現時にリセットできる。定義別に容量を持ち、枯渇時だけ追加で初期化する。
- **Command**。面割り当て1回分を `AssignmentEdit` にまとめる。新規実行、Redo、Undoで面ごとの所属数を更新する共通の `ChangeOwner` を通すため、履歴処理がカウンターの内部表現を意識せずに済む。履歴の操作種別が面割り当て1種類なので、仮想基底は設けていない。
- **Strategy**。`IDebugBot::ChooseAction` はデバッグ行動の選択方策を交換するための契約。敵AIのStateは敵の進行段階に応じて内部から遷移する。Strategyでは管理側が方策を選んで差し替える。
- **Adapter**。`DebugAITestAdapter` は、ゲームの値と入力操作をEngineの観測・実行APIに接続する。ゲーム側の表現に合わせてエンジンの記録・再生を作り直す必要がない。
- **Memento と Dirty Flag**。カメラ編集やデバッグ巻き戻しは、複数フィールドをまとめて元の時点に戻すためスナップショットを使う。輪郭とプレビューは、元データの変更を記録し、描画が必要になった時点で再構築する。前者は復元の一貫性、後者は更新コストの削減が目的。
- **Factory Registry と Resource Cache**。型の選択をシーン登録時に決め、生成時は共通インターフェースを返す。モデルは正規化したパスで再利用する。登録名による生成と、すでに準備したデータの再利用はそれぞれ独立した責務。

### 変更対象外

プレイヤー、武器、シャッター、扉、ステージ進行、ゲームオーバー演出、カメラ、SceneManagerのフェード段階は、初回調査表の理由から現行方式を選んだ。敵の新タイプは定義データで増やせるためFactory Methodの継承階層は設けていない。Observer、Visitor、Composite、Template Methodも、調べた呼び出し・所有関係に導入する実益が確認できなかった。エンジン共通層への追加は不要で、状態と編集コマンドはゲーム層の内部に置いた。

## 課題③ 提出用UML

| 図 | 提出用PNG | 編集用SVG |
| --- | --- | --- |
| 敵AIのState | [1600 × 935 PNG](yanengine-enemy-ai-uml.png) | [SVG](yanengine-enemy-ai-uml.svg) |
| 敵のObject Poolと編集のCommand | [1600 × 1160 PNG](yanengine-pool-command-uml.png) | [SVG](yanengine-pool-command-uml.svg) |

白三角は継承、黒ひし形は所有によるコンポジション、白ひし形は非所有の集約、破線矢印は依存を示す。例えば、`GameScene` はプールを所有し、戦闘中の敵はプールから借りる。個々の `Enemy` を所有するのは `EnemyPool::Slot` の `unique_ptr`。PNGはSVGの要素をWindows WPFで描画して出力し、両図の文字と接続を画像で確認した。

## ビルド・テスト結果

2026年11月21日、Windows／Visual Studio MSBuild・v143／x64。対象バイナリは `generated/design-pattern-verification/` に出力した。

| 検証 | 結果 | 確認範囲 |
| --- | --- | --- |
| ソリューションビルド | **Debug・Release 成功** | `CG2_Setup.sln`、各構成のエラー・警告とも0件。[Debugログ](../generated/design-pattern-verification/build-Debug.log) · [Releaseログ](../generated/design-pattern-verification/build-Release.log) |
| 敵AIの変更前後比較 | **成功** | `27c749d` の実装と24条件×300フレームを比較。移動、向き、攻撃回数・ダメージ、索敵、タイマー、経路、状態、乱数進行が一致。[比較コード](../generated/design-pattern-verification/CompareEnemyAI.cpp) |
| C++単体テスト | **対象9種成功** | `enemy-search`、`enemy-memory`、`enemy-parts`、`enemy-spawn`、`enemy-types`、`weapons`、`weapon-editor`、`stage-progress`、索敵 `enemy-senses`。索敵テストは下記の警告抑制付き。 |
| カメラ・ステージのC++単体テスト | **3種成功** | `camera-sequence`、`camera-path`、`stage-loader`。下記の警告抑制付き。 |
| 敵・プール・敵エディター・Showroom | **Debug実行テスト成功** | `test-enemy-pool.ps1`。部位破壊と破片、個体の再利用、巻き戻し、面の割り当てUndo/Redo、ShowroomのPMX/glTF切替と被弾。 |
| AIとLive Development | **Debug・Release実行テスト成功** | `test-enemy-ai-scene.ps1`。Debugは `-CheckLivePhase3` も実行。DLL・Lua・JSONの一括適用、無効入力、Undo/Redo、プロセス再起動後の復元に成功。 |
| シーン遷移・ゲームオーバー | **Debug・Release実行テスト成功** | `--screen-transition-test` とStage03の `--gameover-sequence-test`。黒フレーム・異常からの復旧、死亡演出、カメラと画面効果、Retry/Title。 |
| 武器・ステージの実行 | **対象テスト成功** | Debugの投擲物、ReleaseのStage03戦闘配置とStage04上層ホテル。武器交換と弾薬、シャッターの襲撃波、脱出演出、結果画面と再開を確認。 |
| Blenderデータ | **対象テスト成功** | Blender 5.0.1。レベル、スポーングループ、スポーン構成スクリプト、部位割り当て、カメラ書き出し・エンジン側参照。カメラのC++部分は下記の警告抑制付き。一部の.blendに新しいバージョンで保存されたという警告あり。 |

一部の単体テストは既存の [HotelEscape.h](../Game/HotelEscape.h) でローカル変数 `duration` が同名メンバを隠す警告 `C4458` を `/W4 /WX` がエラーにする。対象テストに限り、子プロセスのコンパイルオプション `_CL_=/wd4458` で再実行した。標準のソリューションビルド設定は変更していない。

### 失敗・未確認

| 検証 | 状況と理由 |
| --- | --- |
| F7配置エディターの一連の実行テスト | `F7 override removal did not restore Blender base` で停止。着手前からあったDebug実行ファイルでも同じ箇所で再現。テストは現行ステージのサイドカー込みの初期値と、ステージJSONのみを複製したテストデータの復元値を比較する。差が出た以降のF7統合手順は未確認。 |
| Releaseの一時シーン遷移チェック | `Hotel entry / finite gameplay ammo` で停止。テストのStage03という期待に対し、現行のRelease開始登録はStage04ホテル出口。通常の遷移テストとStage04実行テストは成功。 |
| Releaseの旧Stage03ホテル実行テスト | `Hotel placeholder enemies` で停止。着手前のDebug実行ファイルでも再現。テストは敵不在かつスポーン地点6個を期待し、現在のStage03 JSONは16地点中9地点をStageStartに設定している。現行データに対応した戦闘配置テストは成功。 |
| Showroom単体テスト | 警告を抑制して実行し、`ShowroomTests.cpp:65` のStage01武器取得後の `pickedUp` 期待値で失敗。別のD3DテストではShowroomの敵リセットとモデル切替を確認した。 |
| Blenderのアドオン登録・旧スポーン起動・Showroom | 登録テストはバージョン1.13.1を期待し、実体は1.24.0。スポーン起動スクリプトは現行Stage03にも `activation` がないと仮定しており、個別実行で停止。Showroomは保存済みJSONとBlender 5.0.1の再計算結果の一致条件で停止した。これらの入力データとテストは今回の変更範囲外。 |
| 対話操作と外部サービス | 実キーボード・マウスを使った通しプレイ、Blenderの対話操作、Live Developmentの外部AIサービス呼び出しは未実施。実行テストは入力注入・テスト専用起動オプション・ローカルのDLL/Luaを使用した。 |
