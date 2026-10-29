# YanEngine ポリモーフィズム調査・改善報告

調査日: 2026-10-29（日本時間） / 調査開始時のコミット: `1f54eab`

`Game/` と `Engine/` の型宣言、継承、virtual/override、template、コールバック、型・状態による分岐を横断検索し、重点対象は呼び出す側まで追跡した。外部ライブラリ、ビルド対象外の旧ゲーム、生成物は変更対象外。開始時の未コミット変更はなく、前回のカプセル化改善はこのコミットに含まれている。その実装・検証・既存報告書は保持した。

結論: **既存のIScene/SceneManagerが、実際に動く多態性の十分な実例になっている。** 今回はそれを活かし、エンジンに残っていた特定シーン名への依存を1か所取り除いた。敵・武器・演出を種類ごとの継承へ置き換える必要はない。

## 1. 既にポリモーフィズムが実装されていた箇所

### 1.1 ISceneとSceneManager: 提出時の主な実例

| 役割 | 実際のコード位置・メソッド | 内容 |
| --- | --- | --- |
| 共通インターフェース | `Engine/Core/IScene.h:6` `IScene` | `Update()` と `Draw()` は純粋仮想関数。OnEnter/OnExitや各描画段階もvirtual。 |
| 安全な基底型の破棄 | `Engine/Core/IScene.h:8` `virtual ~IScene() = default` | `unique_ptr<IScene>` で所有しても派生クラスのデストラクタが呼ばれる。 |
| 生成方法の登録 | `Engine/Core/SceneManager.h:14` `Factory`、`Engine/Core/GameApp.cpp:324`以降 | TitleSceneやGameSceneを作るラムダを同じFactory型に登録。結果は `unique_ptr<IScene>` になる。 |
| 基底型による所有 | `Engine/Core/SceneManager.h:75` `current_` | 現在のシーンの具象型を固定しない。以前のシーンやGPU完了待ちのシーンも基底型で所有する。 |
| 開始・終了の呼び分け | `Engine/Core/SceneManager.cpp:75` `ChangeScene_()` | 同じ `OnExit()` / `OnEnter()` の呼び出しで、実際のシーンの終了・初期化を行う。 |
| 更新の呼び分け | `Engine/Core/SceneManager.cpp:192` `UpdateScene()` | `current_->Update(app, dt)`。TitleSceneならタイトル更新、GameSceneならゲーム更新になる。 |
| 描画の呼び分け | `Engine/Core/SceneManager.cpp:207`以降の `DrawRender()`、`Draw3D()`、`DrawOverlay2D()` 等 | シーンごとの描画処理を、同じISceneインターフェースから呼ぶ。 |
| 読込の差し替え | `IScene::UpdateLoading()` / `IsReadyForTransition()` / `InitializationError()` | 同期読込は既定実装で済み、準備状態を持つシーンはoverrideできる。現在の全シーンが非同期読込という意味ではない。 |

シーンごとの実装は次のとおり。継承があるだけでなく、SceneManagerから仮想関数が呼ばれることが重要。

| クラス | 宣言位置 | 主なoverride・役割 |
| --- | --- | --- |
| TitleScene | `Game/scene/Flow/TitleScene.h:19` | OnEnter/OnExit/Update/DrawRender/DrawShadow/DrawOverlay2D/DrawImGui。タイトルと射撃・破壊演出。 |
| GameScene | `Game/scene/Main/GameScene.h:32` | OnEnter/OnExit/Update/DrawRender/DrawOverlay2D/DrawImGui。ゲーム進行・戦闘の調整。 |
| GameOverScene | `Game/scene/Flow/GameOverScene.h:5` | OnEnter/OnExit/Update/DrawRender/DrawOverlay2D。独立したシーン。通常FPS内の死亡演出GameOverSequenceとは別の型。 |
| EnemyAITestScene | `Game/scene/Debug/EnemyAITestScene.h:10` | Update/DrawRender/DrawOverlay2D/DrawImGui等。今回UsesSceneViewportもoverride。 |
| EnemyPlacementEditorScene | `Game/scene/Debug/EnemyPlacementEditorScene.h:12` | Update/DrawRender/DrawOverlay2D/DrawImGui等。配置編集。 |
| ClothShowroomScene | `Game/scene/Main/ClothShowroomScene.h:8` | Update/Draw3D/DrawImGui等。Debug/Developmentの布確認画面。 |
| ShowroomScene | `Game/scene/Main/ShowroomScene.h:5` | GameSceneを継承し、コンストラクタでショールーム用途を指定。更新・描画はGameSceneの実装を継承する。 |
| DebugScene | `Game/scene/Debug/DebugScene.h:15` | OnEnter/OnExit/Update/DrawRender/Draw/DrawImGui。既存の検証用シーン。 |
| DebugAITestScene | `Game/scene/Debug/DebugAITestScene.h:12` | OnEnter/OnExit/Update/DrawRender/Draw/DrawImGui。DebugAIアダプターの組込例。 |
| CGTestScene | `Game/scene/Test/CGTestScene.h:13` | OnEnter/OnExit/Update/DrawRender/Draw/DrawImGui。モデル等の検証。 |

GameAppの現在の登録にはTitle、EnemyAITest、配置編集、GameOver、Game、GameOverDemo、Stage03がある。Debug/DevelopmentではShowroomとClothShowroomも登録する。DebugScene、DebugAITestScene、CGTestSceneはソース上の実装であり、現在の標準登録から自動で選択されるとは扱わない。

新しいシーンを追加する場合は、ISceneを実装してFactoryを登録すれば、通常はSceneManagerの更新・描画処理を変更しなくてよい。

### 1.2 DebugAIの既存インターフェース

| 基底型・位置 | 具象型・位置 | 共通メソッドと実際の呼び出し |
| --- | --- | --- |
| `IDebugBot` / `Engine/DebugAI/IDebugBot.h:5` | RandomDebugBot、BasicCombatDebugBot、ApiDebugBot（同ディレクトリの各ヘッダー） | `ChooseAction()` / `Name()`。`Engine/DebugAI/DebugAIManager.cpp:1916` が `IDebugBot* bot_` を通じて判断処理を呼ぶ。ランダム、戦闘ルール、API/コールバックによる判断を同じ形で扱える。 |
| `IGameDebugAdapter` / `Engine/DebugAI/IGameDebugAdapter.h:5` | `Game/scene/Debug/DebugAITestScene.cpp:21` のDebugAITestAdapter | `CaptureDebugState()` / `ExecuteDebugAction()`。Managerはゲームの具体的なクラスを知らずに観測と操作を行う。 |
| `IGenericGameDebugAdapter` / `Engine/DebugAI/Protocol/IGenericGameDebugAdapter.h:5` | 現在のGame/には派生実装・登録を確認できない | 共通観測・汎用操作の契約とManager側の呼出経路はある。ただし「FPS側で汎用アダプターを実装済み」とは説明しない。 |
| `IDebugAITransport` / `Engine/DebugAI/Transport/IDebugAITransport.h:5` | `Engine/DebugAI/Transport/NamedPipeDebugAITransport.h:11` のNamedPipeDebugAITransport | `Poll()` / `TryReceive()` / `Send()` / `Stop()`。Managerは `unique_ptr<IDebugAITransport>` 経由で通信する。現在の具象実装は1種類だが、呼出側は通信方式に依存しない。 |
| `IGenericDebugActionProvider` / `Engine/DebugAI/Protocol/IGenericDebugActionProvider.h:7` | `Engine/DebugAI/Tools/DebugAIViewer/ExternalGenericAIProvider.h:7` | `Configure()` / `ChooseAction()` / `Name()` / `LastStatus()` をoverride。ただし現Viewerは具体型の `gAIProvider` を直接所有・呼出している。複数の具象Providerを基底型で切り替える実運用の例とは区別する。 |

これらの自作基底インターフェースは、いずれもvirtualデストラクタを持っている。ManagerのBot/Adapterは借用ポインタ、Transportは所有ポインタで、寿命の扱いは異なる。今回その契約は変えていない。

DebugAITestSceneはOnEnterでAdapterを登録し、OnExitで登録を外す。現在の通常FPSシーンがこのAdapterを使っているわけではなく、このデバッグ用シーン自体も標準Factory登録には含まれていない。授業の主な説明には、通常起動で使われるISceneを選ぶのが分かりやすい。

### 1.3 SDKのコールバック

`Engine/3D/VideoPlayerMF.h:15` の `XAudio2VoiceCallback : IXAudio2VoiceCallback` は、`OnBufferEnd()` 等をoverrideする。音声APIが共通インターフェースを通して通知する例である。SDKのコールバック契約を利用したもので、敵やシーンの自作階層とは区別する。

## 2. 新しく導入した箇所

### 問題: エンジンが特定シーン名を知っていた

修正前の `Engine/Core/GameApp.cpp:506` では、Releaseの表示経路を次の文字列で決めていた。

```cpp
sceneMgr_->CurrentName() != "EnemyAITest"
```

これは登録名と画面の必要条件を結び付けていた。同じEnemyAITestSceneを別名で登録すると、必要な編集画面を使わなくなる。新しいツールシーンを追加する場合もGameAppの分岐を増やす必要がある。

### 改善: 表示方針をシーンの仮想関数へ

| 修正位置 | 内容 |
| --- | --- |
| `Engine/Core/IScene.h:42` | `virtual bool UsesSceneViewport() const { return false; }` を追加。通常シーンの表示を既定とする。 |
| `Game/scene/Debug/EnemyAITestScene.h:20` | `bool UsesSceneViewport() const override { return true; }`。必要な画面をシーン自身が指定する。 |
| `Engine/Core/SceneManager.h:47` / `Engine/Core/SceneManager.cpp:273` | `UsesSceneViewport()` を公開し、保持する `IScene` へ委譲する。シーンがない場合はfalse。 |
| `Engine/Core/GameApp.cpp:506` | 文字列比較を `!sceneMgr_->UsesSceneViewport()` に置き換える。武器編集画面とポストエフェクト編集画面の既存条件は維持。 |

新しい抽象クラスや、敵・武器の派生クラスは追加していない。既存のISceneに、小さな表示方針の問い合わせを加えただけである。

通常のTitleScene/GameScene等はfalse、EnemyAITestSceneはtrueとなり、現在登録されているシーンの表示経路は以前と同じ。別名で登録してもシーン自身の実装で決まるため、新しい種類に対応する際のGameAppの変更を減らせる。

実行時の追加処理は、従来の文字列比較を置き換えるシーン単位の仮想呼び出し。敵や弾・粒子ごとの仮想呼び出し、新しい毎フレームの動的確保、描画パスの追加はない。速度の数値比較までは行っていない。

## 3. あえて導入しなかった箇所と理由

### 型分岐・状態分岐の調査結果

| 対象・具体的な位置 | 分岐の意味 | 判断・追加時の影響 |
| --- | --- | --- |
| `Game/enemy/EnemyDefinition.h:14` `EnemyTypeName()`、`EnemyDefinitions::Load()` | 種類の表示名、JSON文字列の解釈。 | 文字列/列挙値の変換は継承へ置き換えない。数値だけ異なる敵の追加は定義を増やせるが、新しい意味を持つEnemyTypeならローダー等の追加対応は必要。 |
| `Game/enemy/EnemyAI.h:277` `EnemyAI::Combat()` の `settings.ranged` | 近接と距離維持・射撃の違い。 | 現在の小さい分岐と共通の記憶・経路処理を維持。Normal/Fast/Tankごとのサブクラスは、速度/HP等のデータを重複させる。大幅に異なる攻撃アルゴリズムが増えた時はStrategyの検討余地がある。 |
| `Game/enemy/Enemy.cpp:346` `ApplyBulletDamage()`、`Game/enemy/EnemyExplosion.h:17` `IsBomberDetonationHit()` | Bomberの胴体命中による起爆。 | 限定された仕様判定。種類ごとにEnemyの所有・Pool・部位破壊を別クラスに分ける範囲ではない。 |
| `Game/enemy/EnemyProjectile.h:16` `MakeEnemyProjectile()`、`:34` `StepEnemyProjectile()` | BulletとBombの運動・起爆。 | 値の状態を配列で管理する既存方式を維持。弾ごとの所有ポインタや仮想呼び出しを増やさない。 |
| `Game/WeaponSystem.h:173` `WeaponRuntime::Step()`、`TryFire()`、`UpdateReload()` | Semi/Full/Burst、Magazine/PerRound。 | 発射/装填モードの有限の状態処理。武器IDごとにクラスを作らず、定義をコピーして共通runtimeを動かす。新しい動作モードにはコード追加が必要だが、同じモードの新武器には不要。 |
| `Engine/Effect/EffectManager.cpp:31` `MakeEffectGeometryVertices()`、`Engine/Particle/ParticleManager.cpp:95` `GetOrMakeParticlePrimitiveModel()` | Ring/Sphere/Box等の形状生成。 | 生成時の選択で、実行中のエフェクトそのものの型階層ではない。形状クラスを増やすメリットは小さい。 |
| `Engine/Particle/Particle.cpp:140` `Particle::Update()` | Normal/Hitの2つの計算方法。 | 2種類の小さい処理を維持。CPU/GPUの共通データ、バッチ処理、既存のエディタとの相性を優先。大量の粒子を基底ポインタで所有する方式へ変えない。 |
| `Game/GameOverSequence.cpp` の `Duration()` / `Begin()` 等、`Game/enemy/EnemyAI.h` のEnemyState分岐 | 時間経過に応じた演出・AIの状態。 | 「別の種類のオブジェクト」と「同じオブジェクトの現在状態」は違う。Stateパターンへの全面置換は巻き戻しやタイマーの変更範囲が広く、今回不要。 |
| `Engine/Core/FadeManager.cpp:29` のeasing、描画Common各cppのBlendMode、WinAppのWindowsメッセージ | 補間の種類、GPU設定、OS通知の値による選択。 | 値の分岐として妥当。すべてをvirtualに置き換える必要はない。 |
| `Game/TemporarySceneTransitionCheck.h:49`以降のdynamic_cast | 一時確認機能からTitle/Game固有の状態や操作を扱う。 | 通常の更新・描画を型判定している箇所ではない。確認専用の操作をIScene全体へ追加するとAPIが肥大化するため維持。 |
| `Engine/Core/GameApp.cpp:413` の現在名と要求名の比較 | 同じシーンへの再読込要求かという識別。 | 表示方針の型判定とは異なり、名前による識別が必要なので維持。 |
| `Engine/DebugAI/Tools/DebugAIViewer/ExternalGenericAIProvider.cpp:215` `ChooseAction()` / `GenerateLocalPolicy()` | OpenAI/Geminiの通信・レスポンス形式の違い。 | 今後増えるならバックエンドStrategyが候補。現状は少数の専用ツール処理で、一般Provider契約だけでは扱わない追加操作もあるため今回分割しない。 |

GameHUD、WeaponHUD、StageClearOverlay、GameOverOverlay、GameOverVisionOverlay、UIFontは、シーンやHUDが部品として所有して明示的に呼ぶ合成の設計。現在は異種UIを同じコレクションから一括操作する要求がなく、IWidget等を追加する具体的な効果は小さい。武器種類による表示差も主に渡された定義・弾数・リロード状態で決まる。

また `Game/enemy/EnemyParts.h:113` は `struct EnemyParts : std::vector<EnemyPart>` だが、これは共通インターフェースによる動的多態性の例ではない。vector互換の値コンテナに共有HPを足す使い方で、基底vectorとしてdeleteしてはいけない。現在のEnemyの値メンバとスナップショットを維持し、合成への置き換えは多数の既存の配列操作へ波及するため実施しない。

### 設計評価（指定の6基準）

| 基準 | 評価 |
| --- | --- |
| 共通インターフェースで異なる処理を扱う | ISceneで実際に更新・描画・開始/終了を扱えている。DebugAIにも差し替え可能な契約がある。 |
| 新種類の追加で既存変更を減らす | シーンは実装とFactory登録が中心。今回表示方針もシーン側へ移した。データだけ異なる敵・武器はJSONで増やせる。完全に新しい動作モードではコード変更も必要。 |
| 不要なswitch/ifがない | 特定シーン名で表示を決める分岐は修正。状態・値・データ形式の分岐は理由を付けて維持した。 |
| 継承関係に無理がない | SceneはIScene、BotはIDebugBotという関係は自然。ShowroomはGameSceneの設定違いとして再利用。EnemyPartsのvector継承は動的多態性と混同しない。 |
| virtualのメリットがある | SceneManagerが具象クラスを知らずに呼べる点に明確な効果。今回もその範囲へ限定。自作所有基底型のvirtualデストラクタを確認。 |
| データ駆動の方が適していないか | 敵/武器の数値差、エフェクトの形状・色・時間、粒子のGPU転送データにはデータ駆動と合成を維持した。 |

## 4. template・データドリブンを採用している箇所

以下はすべて同じ意味の多態性ではない。virtualによる実行時の呼び分け、templateによる型に応じた汎用処理、コールバックの差し替え、データによる動作変更を区別して説明する。

| 方法 | 実際のコード | 説明 |
| --- | --- | --- |
| templateによる汎用コンテナ | `Game/DebugTimeline.h:8` `DebugTimeline<State>` | 同じ履歴/巻き戻し処理を異なる状態型に使う。派生状態型や仮想関数を作る必要がない。 |
| templateによる呼び出し可能オブジェクトの差し替え | `Game/enemy/EnemyParts.h:181` `RaycastEnemyPartsTransformed<WorldForPart>()` | 各部位の現在行列を返す関数/ラムダを渡す。衝突判定を再利用し、描画側の行列取得へ依存させない。静的な多態性の具体例。 |
| templateと型制約 | `Engine/DebugAI/DebugInputReplay.h:21` `ProcessInput<T>()` | trivially copyableな入力型に共通の録画/再生処理を使う。static_assertで適用条件も確認する。 |
| templateによるデータ表現 | `Engine/3D/Animation.h:10` `Keyframe<TValue>`、`:20` `AnimationCurve<TValue>` | Vector3とQuaternionのキーフレーム表現を共有。補間自体はAnimationEvaluate.hの型別オーバーロード（線形補間/Slerp）であり、virtualではない。 |
| templateによる保存/読込 | `Engine/3D/ModelCache.h:14`以降のWriter/Readerの `Pod<T>()` / `Vector<T>()`、`Engine/3D/Model.cpp:1219` `PmxReader::Read<T>()` | データ型が違っても共通のバイト処理を使用する。全型を同じ基底クラスへ継承させる用途ではない。 |
| std::function/ラムダ | `SceneManager::Factory`、`Game/Bullet.h:23` BulletTrace/BulletImpact、`Game/enemy/EnemySpawnSystem.h:80` SpawnEnemy、`Engine/3D/Object3d.h:309` PoseModifier | 生成、衝突、出現、ポーズ修正の処理を差し替える。型消去された呼び出し可能オブジェクトであり、自作の継承階層を増やさずに済む。 |
| std::function/ラムダ | `Engine/DebugAI/ApiDebugBot.h:10` ActionProvider/JsonProvider | 判断処理やJSON応答の供給を注入できる。OpenAIDebugActionProvider/GeminiDebugActionProvider自体にはこの目的のvirtual継承がないため、同名メソッドだけで多態性と説明しない。 |
| 敵のデータ駆動 | `Game/enemy/EnemyDefinition.h` EnemyDefinitions::Load、`Enemy::ApplyDefinition()`、`EnemyPool::Acquire()`、`resources/Data/enemies.json` | Normal/Fast/Tank等のHP倍率・速度・部位・見た目を定義として扱う。全敵を同じEnemyで管理しPoolを再利用する。 |
| 武器のデータ駆動 | `Game/WeaponSystem.cpp:68` Load、`Game/WeaponSystem.h:107` WeaponRuntime、`resources/Data/weapons.json` | ID、弾数、間隔、射撃/装填モード、散弾数などから共通runtimeを動かす。Pistol/SMG等の派生クラスを作らない。 |
| 演出のデータ駆動 | `Engine/Effect/EffectManager.cpp:201` LoadEffect、`:332` Play、`:438` Update、`resources/effects/*.json` | テンプレートの粒子ノード/オブジェクトノード、キーフレーム、遅延や寿命をデータで解釈する。EffectTemplateの名前のtemplateはC++のtemplate構文とは別。 |
| 粒子のデータ駆動/GPU | `Engine/Particle/ParticleManager.h:44` EmitterData、`ParticleManager::Update()` / `Emit()`、`resources/particles/*.json` | 色、速度、寿命、形状等を共通レイアウトで処理する。粒子ごとに仮想クラスを持たせない。 |

同じ具象クラスに異なる数値を渡して動きを変えることは、厳密には「異なる派生クラスのvirtual呼び出し」と同じではない。授業で認められたデータドリブンという代替方式として説明できる、という評価である。

## 5. 授業の課題条件を満たしているか

提示された条件に照らすと、**満たしていると判断できる**。

- Game/側にTitleScene、GameScene等のoverrideがあり、Engine/側がIScene経由で実際に呼び分けている。アプリケーション層から実例が消えている状態ではない。
- 基底クラスのポインタから異なる実装を実行し、安全に破棄するコードとテストがある。
- 今回の表示方針にも同じ仕組みを適用し、シーン名による具象種類の判定を減らした。
- 敵・武器・演出に継承を増やさない理由は、共通runtime、JSONの定義、Pool/GPUのデータ処理との相性として説明できる。
- template、オーバーロード、コールバック、データドリブンとvirtualの違いを実コードで区別できる。

授業の追加の採点条件までは確認できないが、「すべての種類を継承にする」ことは今回示された条件にはない。

## 6. 課題提出時に説明できる具体的なコード例

### 例A: 既存のシーン更新を説明する

実コードの関連部分を抜粋する。

```cpp
// Engine/Core/IScene.h
class IScene {
public:
    virtual ~IScene() = default;
    virtual void Update(GameApp& app, float dt) = 0;
    // ほかの仮想関数は省略
};

// Game/scene/Flow/TitleScene.h
class TitleScene : public IScene {
public:
    void Update(GameApp& app, float dt) override;
    // ほかのメンバは省略
};

// Game/scene/Main/GameScene.h
class GameScene : public IScene {
public:
    void Update(GameApp& app, float dt) override;
    // ほかのメンバは省略
};
```

SceneManager内の関連する行は次のとおり。

```cpp
// SceneManager.h
std::unique_ptr<IScene> current_;

// SceneManager.cpp のUpdateScene内
current_->Update(app, dt);
```

処理の流れは、`GameApp::Update()` → `SceneManager::UpdateScene()` → `IScene*` 経由の `Update()` → 現在の実体の `TitleScene::Update()` または `GameScene::Update()` となる。遷移中等のガード処理は別途ある。

口頭では次のように説明できる。

> シーンを管理する側はISceneという共通の型で持っています。同じUpdateを呼んでも、タイトルならタイトル更新、ゲームならプレイヤーや敵の更新が実行されます。virtualで呼び出し先が実際の型に応じて決まり、overrideで各シーンの実装だとコンパイラに確認させています。virtualデストラクタがあるので、基底型の所有ポインタから安全に破棄できます。

### 例B: 今回追加した表示方針を説明する

```cpp
// IScene.h
virtual bool UsesSceneViewport() const { return false; }

// EnemyAITestScene.h
bool UsesSceneViewport() const override { return true; }

// SceneManager.cpp
bool SceneManager::UsesSceneViewport() const {
    return current_ && current_->UsesSceneViewport();
}
```

GameApp::Draw内では次の条件に使う。

```cpp
if (!imgui_->IsWeaponWorkspace() && !sceneMgr_->UsesSceneViewport()
    && !render_->ViewPostDebugVisible()) {
    // 既存の通常ゲーム表示の処理
}
```

> 以前は名前がEnemyAITestかどうかをエンジンが調べていました。今は「このシーンはビューポート画面を使うか」を共通の仮想関数で尋ねています。EnemyAITestSceneはtrue、通常シーンは既定のfalseを返します。シーン名が変わっても、そのシーンの実装で表示方針が決まります。

### 例C: 継承を使わなかった理由を説明する

> 敵の速さやHP、武器の弾数や発射間隔はデータで変えています。同じ処理に違う設定を渡せば表現できるので、武器IDごとにクラスを作ると処理が重複します。処理自体が異なり共通の窓口で切り替えたいシーンにはvirtualを使い、主に設定が違う敵・武器にはデータドリブンを使っています。

## 7. ビルド・テスト結果

### ビルド

`tools/build.ps1 -Configuration Debug` と `-Configuration Release` を実行し、どちらも終了コード0、警告0・エラー0で成功した。x64/v143のソリューションビルドで、CG2_Setup、DirectXTex、DebugAIViewerを含む。

ログは `generated/polymorphism-build-Debug.log` と `generated/polymorphism-build-Release.log`。両構成のゲーム実行ファイルを使って、以下の実行テストも実施した。

### 追加した多態性テスト

`tests/ScenePolymorphismTests.h` を追加し、既存の `tests/ScreenTransitionTests.h` の `RunScreenTransitionTests()` 冒頭から実行する。

| 検証 | 実際に確認すること |
| --- | --- |
| 基底型の安全性 | ISceneが抽象型でvirtualデストラクタを持つことをstatic_assertで確認。 |
| 実際の派生型からの応答 | TitleSceneとEnemyAITestSceneを `unique_ptr<IScene>` で所有し、同じ `const IScene&` の呼出側から、false/trueの異なる表示方針が返ることを確認。 |
| シーン名への非依存 | 異なる2種類のテスト用具象シーンを `RenamedGameplay` / `RenamedTool` として登録。SceneManagerが名前を判定せず、既定/overrideの方針を返すことを確認。空のManagerも確認。 |
| 更新・描画の実装差 | SceneManagerの `unique_ptr<IScene>` 経由で、片方はカウンターを1、もう片方は10増やすUpdate/Drawが実行されることを確認。OnEnter/OnExitの呼出も確認。 |
| 派生デストラクタ | Managerの破棄時、現シーンと退役シーンの両方について、派生デストラクタがそれぞれ1回実行されることを確認。 |

テスト用のGameplayProbe/WorkspaceProbeは検証専用で、ゲームに新しい種類を加えるものではない。実行時の確認は例外で失敗を通知するため、NDEBUGのReleaseでも無効にならない。

このテストを先頭に追加した状態で、Debug/Releaseの `test-screen-transition.ps1` がともにPASSした。したがって、ビルドが通るだけでなく、基底型経由の呼び分けと破棄が両構成で実際に通過している。

### 回帰テスト

CPUテストは4種類を実行し、すべてPASSした。

| スクリプト（tools/） | 確認範囲 |
| --- | --- |
| test-weapons.ps1 | 弾薬、装備、Semi/Full/Burst、装填、無限弾、拾得、旧データ互換、不正定義。 |
| test-enemy-types.ps1 | 敵定義、HP/死亡、遠距離AI、敵弾/爆弾、Pool等。 |
| test-enemy-senses.ps1 | 視野、遮蔽、記憶、追跡/探索/再発見、経路、pause、設定検証。 |
| test-debug-tools.ps1 | 巻き戻しと再実行、武器・敵弾・出現・乱数の復元、設定保存/外部競合。 |

実際のD3D12環境での結果は次のとおり。

| スクリプト（tools/） | Debug | Release | 主な確認範囲 |
| --- | --- | --- | --- |
| test-screen-transition.ps1 | PASS | PASS | 新しい多態性テスト、既存フェード、ロード失敗復旧、入力、実画素被覆、拾得表示。 |
| test-enemy-ai-scene.ps1 | PASS | PASS | 表示経路を変更したAI画面の実行、壁・視認・記憶/探索、ライブ設定、Pool再利用。 |
| test-enemy-pool.ps1 | PASS | 未実施（既存Debug用） | GPU資源再利用、HP/AI/ID、部位破壊、グループ出現、巻き戻し等。前回追加のカプセル化境界チェックも含む。 |
| test-title-scene.ps1 | PASS | 未実施（既存Debug用） | タイトル射撃、文字破壊、演出、実際のGPU描画差、ステージ遷移。 |
| test-camera-runtime.ps1（stage01） | PASS* | PASS* | 通常シーンの表示、開始カメラ、プレイヤー/武器/AI停止、FPS視点への復帰、スキップ等。 |
| test-gameover-runtime.ps1（stage03） | PASS | PASS | 死亡演出、カメラ衝突、入力ガード、HP/武器/敵停止、復帰、実際のGPU描画。 |
| test-hotel-runtime.ps1 | FAIL（既存データとの不一致） | 同左 | 下記の既存の6地点前提で停止。後続は完了していない。 |

実行した組合せは12件中10件PASS。2件は前回と同じホテルテストの不一致である。

`tests/HotelStageChecks.h:98` はスポーン地点6個を期待する一方、`resources/levels/stage03/stage03.json` は13個。このJSONは調査開始時のコミットと同一で、テストの期待値・EnemySpawnSystemも変更していない。両構成とも `FAIL: Six reserved spawns, no active schedule` で停止した。今回の多態性改善とは別の既知課題として残した。

*開始カメラの結果には、非フォーカスの非表示ウィンドウのためSpaceキー入力分岐は未確認、評価処理側のスキップは確認済みと記載されている。ホテル専用の後続経路検証、手操作の長時間通しプレイ、DebugAIのオンラインAPI通信は今回の検証済み範囲に含めない。

詳細ログは `generated/polymorphism-test-*.log`、実行結果の集計は `generated/polymorphism-debug-results.json` / `generated/polymorphism-release-results.json`。最後に `git diff --check` と変更ファイル一覧を確認し、以前のカプセル化コードを含め、予定したシーン/テスト以外の追跡済みファイルに差分がないことを確認した。変更は未コミット。
