#  弾数・定義を読む
player.EquipWeapon(definition);               // 装備する
player.CancelWeaponBurst();                  // 連射予約を止める

auto settings = player.Settings();            // 内部への参照ではなくコピー
settings.moveSpeed = 8;
player.ConfigureMovement(settings);           // 有限・非負か検証してから反映YanEngine カプセル化調査・改善報告

調査日: 2026-10-29（日本時間） / 基準コミット: `c391a99`

対象はこのGitリポジトリの `Game/`、`Engine/`、その呼び出し元と既存テストです。全体の型宣言・公開APIを横断検索し、重点対象は実装と実際の書き換え箇所まで追跡しました。外部ライブラリ `externals/`、ビルド対象外の `archive/legacy-2.5d/`、生成物、リポジトリ外の配布用コピーは変更対象にしていません。開始時の `git status --porcelain=v1` は空でした。変更は未コミットです。

授業の「内部を隠す」は、単に変数に `private` を付けるだけでは達成できません。例えば `private` な武器を `WeaponRuntime&` で返すと、利用側がその武器を丸ごと交換できます。今回は「外から何をしてよいか」をメソッドで表し、読むだけの側には `const` 参照を返すようにしました。

## 1. カプセル化が適切だった部分

| クラス・型 | 確認した良い設計 | 維持した理由 |
| --- | --- | --- |
| Player | `hp_`、移動状態、Transformはprivate。`ApplyDamage()` は非有限値・負値を拒否し、HPを0未満にしない。`GetHP()` / `IsDead()` で観測する。 | HPを任意に代入するsetterは不要。被弾の意味とHP更新が同じクラスにある。 |
| WeaponRuntime | 弾倉・予備弾・リロード段階・バースト・タイマーがprivate。`TryFire()`、`StartReload()`、`Step()` が一緒に更新する。 | 「弾だけ減ったがリロード段階は古い」といった矛盾を避けられる。弾数setterを追加していない。 |
| WeaponSystem | 定義・配置・取得済み状態はprivate。リストの閲覧はconst。`TryPickup()` が装備と取得済みフラグを更新する。ロードとエディタ再読込は一時オブジェクトで検証してから反映する。 | 設定ファイルの不備で、稼働中のデータが半分だけ置き換わるのを防いでいる。 |
| Enemy | 部位HP、AI、モデル、破片、死亡状態はprivate。被弾・爆発・死亡を専用メソッドで処理し、`AI()` / `Definition()` はconst。 | シーンは部位HPの内部配置を知る必要がない。 |
| EnemyPool | スロット・所有権・activeフラグ・定義別バケットはprivate。`Acquire()` / `Release()` に再利用処理をまとめている。`Release()` は死亡・破片寿命も確認する。 | 所有するのはPool、シーンと弾処理は借りたポインタを使う、という既存の責任分担を維持できる。 |
| EnemySpawnSystem | 稼働中の地点・トリガー・グループ、時刻、乱数はprivate。参照はconst。`ActivateSpawn()` / `ActivateGroup()` / `Update()` / `Reset()` で更新する。 | SpawnPoint等のstructのフィールドがpublicでも、システム内部の実体には外から書き込めない。 |
| GameScene / SceneManager / FadeManager | GameSceneの進行・敵一覧・各サブシステム、SceneManagerの遷移段階・現在シーン・所有権、FadeManagerのalpha・時間はprivate。シーン切替には重複防止・ロード失敗時の復旧がある。 | 敵の所有権やフェード管理をシーンへ移す必要はない。 |
| StageProgress / BulletSimulation / BulletManager | 進行状態・撃破ID・弾配列はprivate。ゴールや弾の一覧はconst。シミュレーションと描画を分離している。 | 表示のために内部状態を自由編集する必要がない。 |
| 数学型、定義、イベント、スナップショット | `Vector3`、`Transform`、`Matrix4x4::m`、`WeaponDefinition`、`EnemyDefinition`、`EnemySenses`、ヒット結果やGPU転送構造体は公開データ。 | 値を組み立てて受け渡す型。すべてをGetter/Setterへ置き換えると、意味のない窓口が増える。実行中の所有者が受け取り時に守る方針を採用した。 |

問題のないクラスについては、問題コード・改善・影響範囲は「該当なし、変更なし」です。個別の公開API確認結果は末尾のクラス一覧に記載しています。

## 2. 不十分だった部分

以下の「修正前」は基準コミットの行番号です。修正後は行数が変わるため、3節のメソッド名・末尾の現行クラス位置とあわせて参照してください。

| ID | 修正前の具体的なコード・位置 | なぜ問題か | 改善方法 | 影響範囲 |
| --- | --- | --- | --- | --- |
| F1 | `Game/player/Player.h:24` `FPSMotion::Settings& Settings()` | `Settings().moveSpeed=-1` やNaNを直接入れられる。Playerが受け入れを判断できない。 | 閲覧はconst、変更は値のコピーを編集して `ConfigureMovement()` へ渡す。 | Player、GameSceneの調整UI。既存の閲覧側は変更不要。 |
| F2 | `Game/player/Player.h:26` `WeaponRuntime& CurrentWeapon()`、`Game/scene/Main/GameScene.cpp:198` `player_.CurrentWeapon()=WeaponRuntime{}` | 武器の交換・更新がシーン側から無制限にできる。将来Player側の装備処理を変えると利用側へ広く影響する。 | const閲覧と、装備・解除・取得・バースト取消の用途別メソッドへ分ける。 | GameScene、TitleScene、デモ、ホテルテスト。HUDの読み取りはそのまま。 |
| F3 | `Game/WeaponSystem.h:109` `void Equip(...)`、`:111` `magazine_=definition.magazineSize` | JSONロード時は検証していても、C++で作った定義は直接渡せる。負の弾数・0発消費・NaNのタイマーなどがprivate状態へ入る。 | Equip入口で弾薬・タイマー・モードを検証し、不正ならfalseを返して現在状態を維持する。 | WeaponRuntimeの装備入口、Playerの装備、WeaponSystemの取得。正常な定義の動作は同じ。 |
| F4 | `Game/enemy/Enemy.h:112` `EnemySenses& Senses()`、`Game/scene/Debug/EnemyAITestScene.cpp:255` `auto& s=enemy.Senses()` | NaNの視認距離や範囲外の視野角を、AI内部へ検証なしで代入できる。 | const閲覧、`EnemySenses::IsValid()` と `ConfigureSenses()` を追加。UIはコピー編集後に反映。 | Enemy、AIテストシーン、索敵設定を変更する既存テスト。AIの追跡アルゴリズムは維持。 |
| F5 | `Game/enemy/Enemy.h:73` `ApplyDefinition(...)`、`:123` `SetSpawnIdentity(...)` | 外部で未使用の内部操作が公開されている。定義・モデル・IDをライフサイクルと無関係に変更できる。 | 既存の内部呼び出しを維持したままprivateへ移動。 | Enemy内部のみ。外部呼び出し元がないことを検索し、両構成のビルドで確認。 |
| F6 | `Game/enemy/EnemyProjectile.h:70` `std::vector<EnemyProjectile> projectiles`、`Game/StageProjectile.h:47` `for(auto& p:system.projectiles)` | シーンや描画側が任意の弾を変更・削除できる。弾の寿命更新と整理をシステムが一括管理できない。 | 配列をprivateにし、`Spawn()` / `Clear()` / `Update()` とconst一覧へ。ステージ付き更新も同クラスへ移す。 | GameScene、AIテストシーン、StageProjectile、型・巻き戻しテスト。既存の自由関数は委譲して互換維持。 |
| F7 | `Engine/3D/Model.h:169` `Vector4& GetMaterialColor()`、`:188` `Material* GetMaterial()` | 読み取りのつもりの取得処理から、共有モデルの材質を直接変更できる。未初期化時のstaticダミーも書き換え可能。 | 色は値で返し、材質はconstポインタで返す。既存の明示的な色変更メソッドを使う。 | Model、Object3dの初期材質コピー、Particleの色の読み取り。既存呼び出しのロジック変更不要。 |
| F8 | `Engine/3D/Object3d.h:65` `inline static size_t debugInitializationCount=0` | 外部が計測値を書き換えると、Poolの「GPU初期化なし」という検証の根拠が壊れる。 | カウンターをprivate、取得のみ `DebugInitializationCount()` で公開。 | Debug/DevelopmentのObject3dとPool・Titleテスト。増加箇所は既存のInitializeのみ。 |
| F9 | `Engine/Input/Input.h:39` `POINT prevMousePos_{}`、`:40` `mouseDelta_{}`、`:42` `UpdateMouseDelta()` | マウスの前フレーム状態や差分算出を外部で変更すると、入力状態に矛盾が生じる。通常利用側は値の取得だけでよい。 | 2変数と内部更新メソッドをprivateへ。既存の `GetMouseDelta()` を維持。 | Input。入力注入テストは既存のfriend経由で引き続き動作。 |
| F10 | `Game/scene/Flow/TitleScene.h:32` `Sprite enemyAITestEntry_`、`:33` `enemyPlacementEntry_` | タイトル内部の表示部品を外部が置き換えられる。外部利用もない。 | privateへ移動。 | TitleScene内部のみ。 |
| F11 | `Engine/Camera/Camera.h:17`〜`:21` `fovY_=fovY` 等 | privateでもsetterが無条件代入では、視野角0、aspect0、near>=far、NaNによって投影行列を壊せる。 | 有限性、0<FOV<π、aspect>0、0<near<farを確認。不正値は元の値を保持。平行投影の高さの既存clampは維持しNaNを拒否。 | Cameraの設定入口のみ。正常値・行列更新タイミングは維持。 |
| F12 | `Engine/Particle/ParticleManager.h:178` `ClearGPUBuffers_(ParticleGroup&)` | クラス内部のGPUバッファ消去処理が外部へ公開されていた。呼び出し元は内部だけ。 | privateへ移動。 | ParticleManager内部のみ。 |
| F13 | `Engine/DebugAI/DebugAIManager.h:90` `DebugInputReplay& InputReplay()` | 可変参照によってManagerの録画・再生管理を迂回できる。現在の外部利用はない。 | 可変版を削除しconst版を維持。 | DebugAIManagerの公開APIのみ。 |
| F14 | `Game/player/Player.h:59` `std::clamp(multiplier,...)` | clampだけではNaNを除けず、照準の回転へ伝播する。 | clampの前に有限性を確認する。 | Playerの感度設定。通常のADS感度は同じ。 |

## 3. 実際に修正した内容

- Playerから可変参照をなくした。`EquipWeapon()`、`UnequipWeapon()`、`TryPickupWeapon()`、`CancelWeaponBurst()`、`SetInfiniteAmmo()` を使って操作する。移動設定には一括検証を設けた。HPの任意setterは追加していない。
- WeaponRuntimeの `Equip()` はboolを返す。不正な弾薬・タイマー・列挙値は、以前の武器や進行中のリロードを変えずに拒否する。予備弾の上限へのclamp、時間0のリロード、無限弾、バーストの既存仕様は維持した。バースト数上限64は既存JSONローダーと同じ。
- Enemyの索敵設定はコピー編集後に検証して反映する。AIの現在段階、記憶や経路は引き続きEnemyの内部にある。外部未使用の定義適用とID生成をprivateへ移した。
- EnemyProjectileSystemが弾の追加・更新・削除を管理する。Spawnでは座標・速度・寿命・半径・ダメージなどを検証する。`EnemyProjectile` 自体は引き続き受け渡し用の公開データ。
- Modelの材質取得、Object3dの計測値、InputとTitleSceneの内部メンバ、ParticleManagerの内部処理、DebugAIManagerの再生取得APIを整理した。
- Cameraは不正な投影パラメータを拒否する。通常の値による描画・カメラ演出・更新順は変更していない。
- `tests/EncapsulationChecks.h` を追加。非constの所有者からでも取得結果がconstになることをstatic_assertで確認する。HP、移動設定、索敵、装備失敗時の状態保持、敵弾、Cameraの不正値拒否を実行時にも確認する。既存のPool・ホテル実行テストから呼び出すため、Releaseでもassert無効化の影響を受けない。
- 既存テストは新しい公開APIに合わせて呼び出しだけ変更し、期待する射撃・索敵・描画・再利用の結果は変更していない。

例えば、変更後は次のようになる。

```cpp
const auto& weapon = player.CurrentWeapon(); //
```

## 4. 修正せず残した問題と理由

以下は「publicだから誤り」と判断せず、利用目的や変更範囲を調べた結果です。問題が残るものも明記しています。

| クラス・現行位置・コード | できている部分 / 残る問題 | 改善するときの方法・影響範囲 | 今回残した理由 |
| --- | --- | --- | --- |
| `Engine/Core/SceneManager.h:26` `FadeManager& Fade()` | 遷移状態はprivate。ただし外からFadeをResetでき、遷移段階と表示を不一致にできる。 | 閲覧用const取得と、遷移中を拒否するフェード操作をSceneManagerに用意する。TemporarySceneTransitionCheck・UI・遷移テストに影響。 | 一時シーン確認機能が意図してFadeを操作している。今回その公開操作体系までは置き換えず、既存機能を維持した。 |
| `Engine/3D/Animator.h:39` `GetSkinCluster()`、`:42` `GetPoseSkeleton()` | 時間・再生段階はprivate。ただし骨・GPU構造の可変参照が公開され、骨のインデックスと配列を不一致にできる。 | 骨の編集範囲を限定したコールバック、GPU出力状態更新の専用メソッドへ。Object3d、Cloth、アセット描画テストが対象。 | Object3dが実際に `isUavReady` を更新し、骨には布・ポーズ修正を適用する。描画責任の変更を伴うため残した。 |
| `Engine/3D/Object3d.h:159` `Model* GetModel() const`、`ModelManager::FindModel()` | モデルデータの多くはconstで読める。ただし共有Modelへのポインタは可変で、明示的な材質変更等が他インスタンスにも影響し得る。 | 読み取り・描画用APIと共有アセット編集APIを分離する。Object3d、モデル管理、Particle等へ波及。 | リソースと描画の結合を分ける大きな変更は実施しない。今回の材質取得のconst化は、この問題の一部を改善した。 |
| `Game/enemy/Enemy.h:56` `ResetForSpawn()`、`:57` `RetireFromPool()` | Poolのactive状態はprivateだが、借用Enemyを直接リセットするとPool側の認識とずれる余地がある。 | Pool専用のライフサイクル窓口と、独立Enemyのプレビュー窓口を区別する。Pool、タイトル標的、配置エディタ、再利用テストへ影響。 | タイトルや配置プレビューはPoolに属さないEnemyにも同じ操作を使う。単純なprivate化やfriend追加では用途を保てないため残した。 |
| `Game/StageShutter.h:35` `std::vector<StageShutter> shutters` | 開閉・イベント・衝突同期は専用メソッドにあるが、外部からstateだけ変えると衝突形状との同期を飛ばせる。 | 初期構築用APIとconst一覧、必要ならスナップショット復元APIに分ける。StageLoader、GameScene、ホテル・シャッターテストが対象。 | 現状のローダーはこの配列を組み立て、デバッグ復元も利用する。ステージ側の構築・同期契約をまとめて直す別の改善として残した。 |
| `Game/StageWorld.h:32` `colliders` / `Game/StageLoader.h:14`以降の公開ロード結果 | ステージ定義を作る用途では公開が適切。一方、更新中にColliderだけ編集すると変換行列やシャッターの索引との整合を壊せる。 | 構築データと稼働中Worldを分け、コライダー更新メソッドで行列も更新する。ローダー、衝突、カメラ、エディタ、シャッターが対象。 | 単純なDTOの非公開化では済まない。既存の責任分担を維持するため今回は残した。 |
| `Engine/Physics/ClothSolver.h:56`〜`:62` の設定・粒子・拘束・接触数、`Engine/Physics/ClothComponent.h:22`〜`:25` | 積分器のキャッシュはprivate。ただし稼働中の粒子・拘束を自由変更でき、索引やキャッシュがずれる余地がある。 | 構築時設定・実行時観測・リセット付き再構築を分ける。ClothComponent、Showroom、布・性能・GPUテストへ影響。 | 物理の構築API・調整UI・テストが直接編集を前提としている。布の挙動を変えない制約から別途対応にした。 |
| `Game/scene/Main/ClothShowroomScene.h:17`〜`:26` | モデル所有権はprivate。ただし `Cloth()` / `Character()` の可変参照、時間・カメラ・計測値はpublic。 | 調整設定を値で渡し、時刻と計測値を読み取り専用にする。布UIと実行テストが対象。 | Debug/Development専用の実験画面で、FPS主要経路と切り離されている。布APIと同時に整理するのが適切。 |
| `Game/GameOverSequence.h:90` `Settings settings`、`Game/TitleStartSequence.h:7` `delay`、`Game/enemy/EnemyProceduralAnimation.h:22` `settings` | 実行段階・経過時間はprivate。公開された調整値へNaN等を入れる入口は残る。 | 設定のコピー編集と検証を追加し、進行中の設定変更の扱いも決める。演出UI・死亡演出・アニメーションテストが対象。 | 授業上の改善候補だが、演出調整やテストの直接編集をまとめて置き換える範囲は今回は含めていない。 |
| `Player::RestoreDebug()` / `Enemy::RestoreDebug()`、`Player::RestoreWeaponForDebug()` | 武器型のコピー復元は外部が弾数へ直接代入するのとは異なり、妥当なruntimeを一括で復元できる。公開DebugStateは意図的に編集可能。偽造したスナップショットの完全検証はない。 | 信頼できないデータを復元する用途に拡張する場合は、形式検証・世代・寿命検証を追加する。デバッグ巻き戻しと多数の実行テストへ影響。 | 正確な巻き戻しではタイマー・HP等の同時復元が必要。武器のみの復元はReleaseの既存ホテルテストでも使用するため、名前で用途を明示して維持した。 |
| `WeaponDefinition` / `EnemyDefinition` のプログラムからの受け渡し | JSONは広く検証する。今回のEquip検証は弾薬・タイマー・モードの不変条件を守る。すべての描画・ダメージ設定を包括検証するものではない。 | 構築時・JSON・エディタで使う完全な共通validatorを別途設ける。全ローダーと編集テストに影響。 | 検証ルール全体の統合は避け、今回必要な実行状態の境界に限定した。 |
| GameSceneの責任の多さ / 各種friendテスト | 通常APIは小さく状態はprivate。ただし進行・UI・デバッグ編集の調整を大きなクラスで行う。friendは強いアクセス権を持つ。 | 必要になった時点でUIや演出の責任単位で分離する。シーンの多数の経路に影響。 | 大規模リファクタリングは禁止のため維持。テストのためだけに通常setterを増やしていない。 |
| `Engine/Particle/Particle.h:138`〜`:145` `UpdateNormal()` / `BuildWorld_Normal()` 等 | 粒子とGPU資源はprivateだが、内部の計算用補助メソッドがpublicで、クラス内部の処理順へ依存させる余地がある。 | 使用契約を確認して内部補助をprivateへ移す。Particleの旧デバッグ表示と利用側のAPIを確認する。 | 主要FPSの弾薬・敵弾とは独立した既存の粒子APIで、今回はGPU資源を直接消去するParticleManager側の不要公開を優先して修正した。 |

`WeaponEditor::entries`、`EnemyPlacements::groups`、`CameraSequenceOverrides::sequences`、`DebugJsonEditor::document` は編集する文書そのものなので公開を維持した。保存・反映時の検証を使う設計であり、稼働中のHPや弾数とは扱いが違う。`Bullet` はclass表記でも内容は弾生成用の値で、BulletSimulationへ渡すとコピーされ、内部一覧はconstとなるため変更していない。

DirectXのデバイス・コマンドリスト・リソース、GameAppから各サービスへのポインタは、描画や入力の操作に必要な借用インターフェースとして維持した。constポインタにすれば常に正解、というものではない。所有権、呼ぶ時期、GPU完了待ちという契約は引き続き必要。

## 5. カプセル化の6項目それぞれの達成状況

| 授業の基準 | 判定 | 今回の確認・改善と限界 |
| --- | --- | --- |
| 1. 状態と操作が適切なクラスにまとまる | 概ね達成 | HPはPlayer、弾薬はWeaponRuntime、再利用はPool、出現予定はSpawnSystem。敵弾のステージ付き更新も所有者へ移した。GameSceneは調整役として大きいまま。 |
| 2. 不要なメンバ変数が非公開 | 改善、一部残る | Input、TitleScene、Object3d計測値、敵弾配列を非公開化。Cloth・Shutter等には課題が残る。値型・DTOの公開は妥当と判断。 |
| 3. 状態変更が適切なメソッド経由 | FPS主要経路で改善 | 武器取得・装備・解除・索敵調整・弾生成を専用メソッドへ。既存の被弾・射撃・リロードは良好。公開の物理データ・調整値は例外として残る。 |
| 4. 不正値・矛盾から保護される | 重点部分を改善、全域の保証なし | 不正な移動・索敵設定、弾薬・タイマー、敵弾、投影設定を拒否。HPは既存の保護を確認。任意のDebugStateや全パラメータの完全検証は未実施。 |
| 5. 内部変更の影響が小さい | 改善 | 利用側から武器・索敵・材質・弾配列の書き換えを外した。GPUや骨編集の深い結合、シーン/Poolの借用寿命の契約は残る。 |
| 6. 責任と公開APIが理解しやすい | 概ね達成 | 「読む」「装備」「拾う」「被弾」「出現」「返却」の意味が明確。内部未使用APIを非公開にした。GameScene・Cloth・フェードの公開範囲には次の整理余地がある。 |

6項目がプロジェクト全域で完全達成した、とは評価していない。安全性は「弾数が負にならない」、一貫性は「リロードと弾数を一緒に更新する」、柔軟性は「内部の弾データを変えてもHUDは読むだけ」、理解しやすさは「何をする操作かが名前から分かる」という具体例で比較できる。

## 6. ビルド・動作確認結果

### ビルド

`tools/build.ps1 -Configuration Debug` と `-Configuration Release` を実行した。どちらもx64/v143のソリューションビルドが終了コード0、警告0・エラー0で成功した。ソリューションのCG2_Setup、DirectXTex、DebugAIViewerを含む。ゲーム実行ファイルはリポジトリの一階層上の `generated/outputs/{Debug,Release}/CG2_Setup.exe`。

ログ: `generated/encapsulation-build-Debug.log`、`generated/encapsulation-build-Release.log`。新しい境界チェックのstatic_assertも両構成でコンパイル済み。

### CPUテスト

| スクリプト（tools/） | 結果 | 確認範囲 |
| --- | --- | --- |
| test-weapons.ps1 | PASS | 弾薬・空撃ち・リロード・装備取消・バースト・無限弾・拾得・ADS・不正定義 |
| test-weapon-editor.ps1 | PASS | 作成/複製/検証/保存、外部編集競合、取得状態保持、装備リセット |
| test-bullets.ps1 | PASS | 遅延命中・高速弾・部位・壁優先・寿命/距離・巻き戻し・散弾と消費弾数 |
| test-enemy-types.ps1 | PASS | 定義・HP/死亡・遠距離AI・敵弾・爆弾・プール/後片付け |
| test-enemy-spawn.ps1 | PASS | 出現スケジュール、起動、上限、リセット等の既存アサーション |
| test-enemy-parts.ps1 | PASS | 部位・HP・共有HP等の既存アサーション |
| test-enemy-senses.ps1 | PASS | 視野・視認時間・遮蔽・記憶・探索/再発見・一時停止・経路・旧JSON検証 |
| test-fps.ps1 | PASS | フレーム間隔・斜め移動速度・カメラ方向・ピッチ制限・マウス差分 |
| test-debug-tools.ps1 | PASS | 巻き戻し/進め直し・敵弾/武器/出現/乱数の復元・設定保存/競合 |
| test-stage-progress.ps1 | PASS | ゴール・ステージクリア・時間・撃破集計等の既存アサーション |

### 実際のD3D12環境での実行テスト

| スクリプト（tools/） | Debug | Release | 確認範囲 |
| --- | --- | --- | --- |
| test-enemy-pool.ps1 | PASS | 未実施（既存Debug用） | 実際のGPU資源再利用、再出現時のHP/AI/ID、グループ同時起動、巻き戻し、描画・命中位置 |
| test-enemy-ai-scene.ps1 | PASS | PASS | 実際の壁による遮蔽、記憶からの探索・再発見、設定変更、同じPoolスロットの再利用 |
| test-title-scene.ps1 | PASS | 未実施（既存Debug用） | タイトル射撃、文字破壊、GPU描画差、演出、ステージへの遷移、無限弾等 |
| test-screen-transition.ps1 | PASS | PASS | フェード・重複/再入要求・ロード失敗復旧・入力・GPU全画素の被覆・拾得表示 |
| test-gameover-runtime.ps1（stage03） | PASS | PASS | 被弾後のカメラと壁、HP/武器/敵の停止、入力ガード、演出・復帰、描画 |
| test-camera-runtime.ps1（stage01） | PASS* | PASS* | 開始演出中のPlayer/武器/AI/Pool停止、FPSカメラへの復帰、スキップ・リスタート |
| test-hotel-runtime.ps1 | FAIL（既存の期待とデータが不一致） | 同左 | 後述。ホテル専用の後続検証は停止したため完了扱いにしない。 |

実行テストの組合せは12件中10件PASS、2件が同じホテル前提不一致でFAIL。詳細ログは `generated/encapsulation-test-*-{Debug,Release}.log`、集計は `generated/encapsulation-validation.json`。

追加した `RunEncapsulationChecks()` はDebugのPoolテストで通過した。またDebug/Release双方のホテルテストは、このチェックを最初に通過した後で既存のホテル検証に失敗している。HPの不正ダメージ拒否、const取得、移動/索敵の不正値拒否、装備失敗時の弾数・リロード保持、敵弾の追加と読み取り、Cameraの不正値後の行列保持を検証した。

ホテルテストの失敗箇所は `tests/HotelStageChecks.h:98` の次の期待。

```cpp
Check(spawns.Points().size()==6 && spawns.Triggers().empty() && spawns.Groups().empty(),
      "Six reserved spawns, no active schedule");
```

現在の `resources/levels/stage03/stage03.json` はspawnPointsが13個、spawnTriggersとspawnGroupsが0個。このJSONは基準コミットHEADと同一で、上記テストとSpawnローダーも今回変更していない。したがって6個という古い前提と現在データの不一致を確認できる。期待値の変更やステージデータ修正は本依頼のカプセル化改善に含めず、そのまま残した。

*開始カメラのテスト出力は、非フォーカスの非表示ウィンドウのためSpaceキー入力の分岐を未確認と明記している。演出評価側のスキップは検証済み。手操作による長時間の通しプレイは行っていない。ホテルテストが停止した後のシャッター経路検証やStage02/Showroomの後続回帰も未完了。ゲーム仕様・正常データの射撃/リロード/索敵などを変える意図の修正はしていないが、これらの未確認範囲まで動作保証したとは扱わない。

最後に `git diff --check` を実行し、差分の空白エラーがないことも確認した。

## 付録: クラス別の公開API確認一覧

以下はクラス宣言ごとの確認位置。継承されたシーン、DebugAI補助ツール、cpp内の補助クラスも含む。問題がある行はF番号または4節に対応し、理由・改善案・影響範囲はそちらに記載した。問題なしは今回のカプセル化観点で追加修正を必要とする根拠が見つからなかったという意味で、全機能の正しさを保証するものではない。

クラス宣言113件（ネスト・cpp内補助型を含む）を一覧化。

| クラス | 現行ファイル・行 | 判定 | 良い部分・課題・対応 |
| --- | --- | --- | --- |
| Bullet | `Game/Bullet.h:7` | 維持・値型 | 生成用の値データ。BulletSimulationへコピーされ、内部一覧はconstとなる。 |
| BulletSimulation | `Game/Bullet.h:34` | 良好 | 弾や描画資源をprivate管理。生成・更新・消去は専用操作。スナップショットは値の単位。 |
| BulletManager | `Game/BulletManager.h:12` | 良好 | 弾や描画資源をprivate管理。生成・更新・消去は専用操作。スナップショットは値の単位。 |
| CameraSequence | `Game/CameraSequence.h:35` | 良好 | 再生時間・段階・キャッシュをprivate管理。再生・停止・評価を公開。 |
| CameraSequenceOverrides | `Game/CameraSequenceOverrides.h:14` | 維持・編集文書 | 公開データは調整中の文書。保存・反映時に検証する。実行中HP等とは扱いを区別。 |
| DebugJsonEditor | `Game/DebugJsonEditor.h:15` | 維持・編集文書 | 公開データは調整中の文書。保存・反映時に検証する。実行中HP等とは扱いを区別。 |
| DebugTimeline | `Game/DebugTimeline.h:8` | 良好 | 履歴とカーソルprivate。過去・未来の取得はconst Stateポインタ。 |
| Enemy | `Game/enemy/Enemy.h:37` | 修正 F4/F5・4節 | HP・AI・描画private。索敵をコピー反映へ。外部未使用の定義適用とID生成を非公開化。Pool/Debugの契約は残る。 |
| EnemyDefinitions | `Game/enemy/EnemyDefinition.h:48` | 良好 | 定義マップprivate、Find/Allはconst。検証後に差し替える。 |
| EnemyPlacements | `Game/enemy/EnemyPlacements.h:28` | 維持・編集文書 | 公開データは調整中の文書。保存・反映時に検証する。実行中HP等とは扱いを区別。 |
| EnemyPool | `Game/enemy/EnemyPool.h:28` | 概ね良好・4節 | スロット所有とactiveはprivate。Acquire/Releaseで管理。借用Enemyのライフサイクル契約は残る。 |
| EnemyProceduralAnimation | `Game/enemy/EnemyProceduralAnimation.h:20` | 一部課題・4節 | 位相private、調整settingsはpublic。不正な調整値の包括検証は残る。 |
| EnemyProjectileSystem | `Game/enemy/EnemyProjectile.h:69` | 修正 F6 | 弾一覧private、const閲覧、検証付きSpawnとUpdate/Clearに管理を集約。 |
| EnemySpawnSystem | `Game/enemy/EnemySpawnSystem.h:76` | 良好 | 稼働地点・時刻・乱数private。出現/更新/リセットの操作とconst閲覧を公開。 |
| GameHUD | `Game/GameHUD.h:7` | 良好 | ゲーム状態をconstで受け取り表示する。HPや弾薬の実体を直接変更しない。 |
| GameOverOverlay | `Game/GameOverOverlay.h:4` | 良好 | 選択状態private。Selectは有効なメニュー番号のみ受け入れる。 |
| GameOverSequence | `Game/GameOverSequence.h:10` | 一部課題・4節 | 演出の段階・時刻・身体はprivate。公開調整settingsの不正値検証には改善余地。 |
| GameOverVisionOverlay | `Game/GameOverVisionOverlay.h:8` | 良好 | 表示部品や使用数はprivate。必要な結果・表示設定を受け取って描画する。 |
| Player | `Game/player/Player.h:8` | 修正 F1/F2/F14 | HP保護を維持。武器と設定はconst閲覧と用途別操作。Debug復元の例外は4節。 |
| DebugAITestAdapter | `Game/scene/Debug/DebugAITestScene.cpp:21` | 良好 | 借用scene参照private。観測値の取得とシーンのデバッグ操作へ委譲する。 |
| DebugAITestScene | `Game/scene/Debug/DebugAITestScene.h:12` | 概ね良好 | シーン主要状態private。OnEnter/Update/Draw等のライフサイクルや用途別操作を公開。 |
| DebugScene | `Game/scene/Debug/DebugScene.h:15` | 概ね良好 | シーン主要状態private。OnEnter/Update/Draw等のライフサイクルや用途別操作を公開。 |
| EnemyAITestScene | `Game/scene/Debug/EnemyAITestScene.h:10` | 修正 F4/F6の呼出側 | 主要状態private。索敵コピー反映と弾Spawnへ移行。表示はconstで読む。 |
| EnemyPlacementEditorScene | `Game/scene/Debug/EnemyPlacementEditorScene.h:12` | 概ね良好・4節 | 選択・プレビュー所有private。編集文書と独立Enemyのライフサイクル操作を維持。 |
| GameOverScene | `Game/scene/Flow/GameOverScene.h:5` | 概ね良好 | シーン主要状態private。OnEnter/Update/Draw等のライフサイクルや用途別操作を公開。 |
| TitleScene | `Game/scene/Flow/TitleScene.h:19` | 修正 F2/F10 | 表示部品を非公開化し、武器操作をPlayerへ委譲。独立標的の寿命契約は4節。 |
| ClothShowroomScene | `Game/scene/Main/ClothShowroomScene.h:8` | 課題あり・4節 | モデル所有private。publicな時間・カメラ・計測値とCloth/Characterの可変参照は残る。 |
| GameScene | `Game/scene/Main/GameScene.h:32` | 修正・一部課題 | 主要状態private。Playerと敵弾の操作入口を改善。Shutter/friend/責任の広さは4節。 |
| ShowroomScene | `Game/scene/Main/ShowroomScene.h:5` | 良好 | GameSceneの用途をコンストラクタで指定するだけで、追加の公開状態はない。 |
| CGTestScene | `Game/scene/Test/CGTestScene.h:13` | 概ね良好 | シーン主要状態private。OnEnter/Update/Draw等のライフサイクルや用途別操作を公開。 |
| StageClearOverlay | `Game/StageClearOverlay.h:7` | 良好 | 表示部品や使用数はprivate。必要な結果・表示設定を受け取って描画する。 |
| StageLoader | `Game/StageLoader.h:12` | 維持・一部課題 | 公開ロード結果は構築データ。稼働中Worldとの境界は4節。 |
| StageProgress | `Game/StageProgress.h:25` | 良好 | 時刻・進行・撃破IDprivate。更新/撃破観測/リセットをまとめる。 |
| StageShutterSystem | `Game/StageShutter.h:33` | 課題あり・4節 | イベントprivateだがshuttersはpublic。状態と衝突同期を迂回できる。 |
| StageWorld | `Game/StageWorld.h:28` | 一部課題・4節 | 衝突演算は集約。colliders等の構築データ公開と行列更新の契約が残る。 |
| StartPlayerVisual | `Game/StartPlayerVisual.h:9` | 概ね良好 | 身体と布の所有を内部に保持し、初期化・姿勢更新・描画を提供。 |
| TemporarySceneTransitionCheck | `Game/TemporarySceneTransitionCheck.h:11` | 一部課題・4節 | 確認機能の状態private。SceneManagerの可変Fadeへ依存する。 |
| TitleStartSequence | `Game/TitleStartSequence.h:5` | 一部課題・4節 | 開始状態と経過private、delayはpublic。不正な演出設定の入口は残る。 |
| UIFont | `Game/UIFont.h:8` | 良好 | 表示部品や使用数はprivate。必要な結果・表示設定を受け取って描画する。 |
| WeaponEditor | `Game/WeaponEditor.h:9` | 維持・編集文書 | 公開データは調整中の文書。保存・反映時に検証する。実行中HP等とは扱いを区別。 |
| WeaponEditorUI | `Game/WeaponEditorUI.h:9` | 維持・UI状態 | 公開の表示/モード/戻り指定は編集画面の制御用。弾薬runtimeを公開しない。 |
| WeaponHUD | `Game/WeaponHUD.h:8` | 良好 | ゲーム状態をconstで受け取り表示する。HPや弾薬の実体を直接変更しない。 |
| WeaponRuntime | `Game/WeaponSystem.h:107` | 修正 F3 | 弾薬・タイマーprivate。Equipでも検証し、不正定義で進行中状態を壊さない。 |
| WeaponSystem | `Game/WeaponSystem.h:277` | 修正 F3の呼出側 | 公開一覧はconst。装備成功を確認してから拾得済みにする。 |
| Sprite | `Engine/2D/Sprite.h:12` | 概ね良好 | 位置・色・GPU資源private。必要な表示操作とconst/値の取得を公開。 |
| SpriteCommon | `Engine/2D/SpriteCommon.h:7` | 概ね良好 | 描画設定・所有資源private。初期化/描画/必要なサービス取得を公開。GPU・Camera借用の契約を維持。 |
| TextureManager | `Engine/2D/TextureManager.h:12` | 概ね良好 | キャッシュと所有資源private。準備データは値型。GPUハンドルは必要な借用APIとして維持。 |
| Animator | `Engine/3D/Animator.h:10` | 一部課題・4節 | 再生時間private。骨とSkinClusterの可変参照はObject3d・Clothとの結合として残る。 |
| PmxReader | `Engine/3D/Model.cpp:1204` | 良好 | バイナリ・読み取り位置・エラーprivate。ReadBytesは残りサイズを確認。 |
| Model | `Engine/3D/Model.h:15` | 修正 F7・4節 | 材質取得をconst/値へ。共有モデル全体の可変ポインタ問題は残る。 |
| ModelCommon | `Engine/3D/ModelCommon.h:4` | 概ね良好 | 描画設定・所有資源private。初期化/描画/必要なサービス取得を公開。GPU・Camera借用の契約を維持。 |
| ModelManager | `Engine/3D/ModelManager.h:9` | 一部課題・4節 | モデル所有と非同期準備private。共有モデルの可変ポインタを返す。 |
| Object3d | `Engine/3D/Object3d.h:26` | 修正 F8・4節 | Transform/GPU資源private、計測値を読み取り専用化。共有Model・骨との結合は維持。 |
| Object3dCommon | `Engine/3D/Object3dCommon.h:9` | 概ね良好 | 描画設定・所有資源private。初期化/描画/必要なサービス取得を公開。GPU・Camera借用の契約を維持。 |
| Object3dLight | `Engine/3D/Object3dLight.h:7` | 概ね良好 | ライトとGPU定数を内部管理。設定操作と値/const閲覧を提供。 |
| PrimitiveCommon | `Engine/3D/PrimitiveCommon.h:9` | 概ね良好 | 描画設定・所有資源private。初期化/描画/必要なサービス取得を公開。GPU・Camera借用の契約を維持。 |
| RingMesh | `Engine/3D/RingMesh.h:10` | 概ね良好 | 描画設定・所有資源private。初期化/描画/必要なサービス取得を公開。GPU・Camera借用の契約を維持。 |
| Skeleton | `Engine/3D/Skeleton.h:13` | 良好 | 骨配列・名前索引・rootはprivate。構築/アニメ適用/更新の操作を公開。 |
| SkinnedModel | `Engine/3D/SkinnedModel.h:58` | 概ね良好 | 骨・頂点・再生時間・GPU資源private。骨一覧の取得はconst。 |
| SkinningCommon | `Engine/3D/SkinningCommon.h:6` | 概ね良好 | 描画設定・所有資源private。初期化/描画/必要なサービス取得を公開。GPU・Camera借用の契約を維持。 |
| Skybox | `Engine/3D/Skybox.h:9` | 概ね良好 | 描画設定・所有資源private。初期化/描画/必要なサービス取得を公開。GPU・Camera借用の契約を維持。 |
| SkyboxCommon | `Engine/3D/SkyboxCommon.h:5` | 概ね良好 | 描画設定・所有資源private。初期化/描画/必要なサービス取得を公開。GPU・Camera借用の契約を維持。 |
| VideoPlayerMF | `Engine/3D/VideoPlayerMF.h:34` | 概ね良好 | 再生状態と映像/音声資源をprivate管理。ロード・再生・停止等を操作として公開。 |
| AudioSystem | `Engine/Audio/AudioSystem.h:20` | 概ね良好 | 再生状態と映像/音声資源をprivate管理。ロード・再生・停止等を操作として公開。 |
| Camera | `Engine/Camera/Camera.h:7` | 修正 F11 | Transform/投影設定/行列private。不正なFOV/aspect/clipを拒否する。 |
| DebugCamera | `Engine/Camera/DebugCamera.h:7` | 概ね良好 | 位置と入力連携の内部状態は非公開。用途に必要な取得と操作を維持。 |
| DirectXCommon | `Engine/Core/DirectXCommon.h:16` | 概ね良好・4節 | GPU資源・パス・同期/割当/遷移状態private。低水準APIのポインタ/ハンドルは描画用途で維持。 |
| TextureUploadBatch | `Engine/Core/DirectXCommon.h:80` | 良好 | アップロード開始/終了をRAIIで対応付ける。dxポインタprivate。 |
| FadeManager | `Engine/Core/FadeManager.h:17` | 良好・所有者側課題 | alpha/段階/時間private、設定取得const。可変Fade公開はSceneManager側の課題。 |
| GameApp | `Engine/Core/GameApp.h:27` | 概ね良好 | サービス所有権と終了フラグprivate。サービスへの必要な借用APIを維持。 |
| ImGuiManagaer | `Engine/Core/ImGuiManagaer.h:19` | 概ね良好 | 描画設定・所有資源private。初期化/描画/必要なサービス取得を公開。GPU・Camera借用の契約を維持。 |
| IScene | `Engine/Core/IScene.h:6` | 概ね良好 | 次シーン名private。要求/消費操作と各シーンの仮想ライフサイクルを定義。 |
| SceneManager | `Engine/Core/SceneManager.h:12` | 一部課題・4節 | 遷移・所有権・ロード復旧を集約。Fadeの可変取得は残る。 |
| SrvManager | `Engine/Core/SrvManager.h:5` | 概ね良好・4節 | GPU資源・パス・同期/割当/遷移状態private。低水準APIのポインタ/ハンドルは描画用途で維持。 |
| WinApp | `Engine/Core/WinApp.h:23` | 概ね良好 | HWND/ウィンドウクラス/終了ガードprivate。定数と必要なハンドルを公開。 |
| ApiDebugBot | `Engine/DebugAI/ApiDebugBot.h:8` | 概ね良好 | 判断・通信の内部状態private。設定/判断/観測結果取得を公開。一般設定値の完全検証までは保証しない。 |
| BasicCombatDebugBot | `Engine/DebugAI/BasicCombatDebugBot.h:9` | 概ね良好 | 判断・通信の内部状態private。設定/判断/観測結果取得を公開。一般設定値の完全検証までは保証しない。 |
| DebugAIManager | `Engine/DebugAI/DebugAIManager.h:28` | 修正 F13 | 設定・録画・再生状態private。外部未使用の可変InputReplay取得を削除。 |
| DebugAnomalyDetector | `Engine/DebugAI/DebugAnomalyDetector.h:25` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| DebugGenericActionReplay | `Engine/DebugAI/DebugGenericActionReplay.h:20` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| DebugInputReplay | `Engine/DebugAI/DebugInputReplay.h:10` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| DebugLogger | `Engine/DebugAI/DebugLogger.h:9` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| DebugObservationEventRecorder | `Engine/DebugAI/DebugObservationEventRecorder.h:15` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| DebugReplayPlayer | `Engine/DebugAI/DebugReplayPlayer.h:22` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| DebugReplayRecorder | `Engine/DebugAI/DebugReplayRecorder.h:18` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| GeminiDebugActionProvider | `Engine/DebugAI/GeminiDebugActionProvider.h:11` | 概ね良好 | 判断・通信の内部状態private。設定/判断/観測結果取得を公開。一般設定値の完全検証までは保証しない。 |
| IDebugBot | `Engine/DebugAI/IDebugBot.h:5` | 良好・インターフェース | 役割を仮想メソッドで公開する型。実行状態のpublic変数なし。 |
| IGameDebugAdapter | `Engine/DebugAI/IGameDebugAdapter.h:5` | 良好・インターフェース | 役割を仮想メソッドで公開する型。実行状態のpublic変数なし。 |
| OpenAIDebugActionProvider | `Engine/DebugAI/OpenAIDebugActionProvider.h:11` | 概ね良好 | 判断・通信の内部状態private。設定/判断/観測結果取得を公開。一般設定値の完全検証までは保証しない。 |
| IGenericDebugActionProvider | `Engine/DebugAI/Protocol/IGenericDebugActionProvider.h:7` | 良好・インターフェース | 役割を仮想メソッドで公開する型。実行状態のpublic変数なし。 |
| IGenericGameDebugAdapter | `Engine/DebugAI/Protocol/IGenericGameDebugAdapter.h:5` | 良好・インターフェース | 役割を仮想メソッドで公開する型。実行状態のpublic変数なし。 |
| RandomDebugBot | `Engine/DebugAI/RandomDebugBot.h:8` | 概ね良好 | 判断・通信の内部状態private。設定/判断/観測結果取得を公開。一般設定値の完全検証までは保証しない。 |
| DiagnosticBuilder | `Engine/DebugAI/Tools/DebugAIViewer/AdapterDiagnostics.cpp:204` | 維持・局所集計 | cpp内の集計ビルダー。checks/件数はレポート作成用の結果データ。 |
| AdapterDiagnostics | `Engine/DebugAI/Tools/DebugAIViewer/AdapterDiagnostics.h:19` | 良好 | 診断/レポート生成の入口を公開。結果は受け渡し用データ。 |
| CoverageTracker | `Engine/DebugAI/Tools/DebugAIViewer/CoverageTracker.h:13` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| ExternalGenericAIProvider | `Engine/DebugAI/Tools/DebugAIViewer/ExternalGenericAIProvider.h:7` | 概ね良好 | 判断・通信の内部状態private。設定/判断/観測結果取得を公開。一般設定値の完全検証までは保証しない。 |
| ScenarioRunner | `Engine/DebugAI/Tools/DebugAIViewer/ScenarioRunner.h:12` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| TestReportGenerator | `Engine/DebugAI/Tools/DebugAIViewer/TestReportGenerator.h:13` | 良好 | 診断/レポート生成の入口を公開。結果は受け渡し用データ。 |
| IDebugAITransport | `Engine/DebugAI/Transport/IDebugAITransport.h:5` | 良好・インターフェース | 役割を仮想メソッドで公開する型。実行状態のpublic変数なし。 |
| NamedPipeDebugAITransport | `Engine/DebugAI/Transport/NamedPipeDebugAITransport.h:11` | 概ね良好 | 記録・進行・通信/集計状態private。開始/停止/記録/観測とconst/値の取得を公開。 |
| EffectManager | `Engine/Effect/EffectManager.h:15` | 概ね良好 | テンプレートと実行中エフェクトの所有はprivate。ロード/再生/更新/消去の操作を公開。 |
| Input | `Engine/Input/Input.h:8` | 修正 F9 | 前回マウス位置・差分・内部更新を非公開化。利用側は差分の値を取得する。 |
| LevelLoader | `Engine/Level/LevelLoader.h:10` | 維持・DTO入口 | LevelData/ObjectDataを構築するローダー。結果の値型公開は適切。 |
| Matrix4x4 | `Engine/math/Matrix4x4.h:8` | 維持・値型 | m[4][4]は数学の値データ。演算/GPU転送に使うためprivate化しない。 |
| Particle | `Engine/Particle/Particle.h:22` | 改善余地・4節 | 粒子と描画資源の実体はprivate。生成/計算の補助操作はpublicが広め。 |
| ParticleCommon | `Engine/Particle/ParticleCommon.h:5` | 概ね良好 | ブレンド・PSO所有状態private。GPU処理用操作と設定を維持。 |
| ParticleManager | `Engine/Particle/ParticleManager.h:153` | 修正 F12 | グループ所有private。内部GPUバッファ消去を非公開化。 |
| ClothComponent | `Engine/Physics/ClothComponent.h:7` | 課題あり・4節 | 積分キャッシュ等はprivate。稼働中の粒子/拘束/接触数などはpublicで、整合性を崩す余地。 |
| ClothSolver | `Engine/Physics/ClothSolver.h:43` | 課題あり・4節 | 積分キャッシュ等はprivate。稼働中の粒子/拘束/接触数などはpublicで、整合性を崩す余地。 |
| DirectionalShadowMap | `Engine/Render/DirectionalShadowMap.h:8` | 概ね良好・4節 | GPU資源・パス・同期/割当/遷移状態private。低水準APIのポインタ/ハンドルは描画用途で維持。 |
| OffscreenPass | `Engine/Render/OffscreenPass.h:9` | 概ね良好・4節 | GPU資源・パス・同期/割当/遷移状態private。低水準APIのポインタ/ハンドルは描画用途で維持。 |
| RenderManager | `Engine/Render/RenderManager.h:46` | 概ね良好・4節 | GPU資源・パス・同期/割当/遷移状態private。低水準APIのポインタ/ハンドルは描画用途で維持。 |
| Timer | `Engine/Utility/AssetLoadProfile.h:21` | 良好 | 計測開始時刻と対象名private。スコープ終了時の記録を担当。 |

### 構造体等の扱い

構造体宣言は定義・編集文書・イベント・結果・CPU状態・GPU転送データを含む。EnemyAI/EnemyParts/EnemyNavigation/EnemyObservationMemoryは操作も持つCPU状態だが、実行中の実体はEnemyがprivateに所有し、AI取得もconstなので、一律に非公開化していない。ClothやShutter等は4節で別に指摘した。以下にファイル別の宣言位置を残す。

| ファイル | 構造体名（現行行番号） |
| --- | --- |
| `Game/Bullet.h` | BulletHit (14) |
| `Game/BulletManager.h` | BulletEnemyImpact (5) |
| `Game/CameraSequence.h` | CameraPose (14), CameraSequencePoint (18), CameraSequenceDefinition (29), ArcTable (325), PathCache (326) |
| `Game/enemy/DetachedEnemyPart.h` | DetachedPartSettings (4), DetachedPartMotion (19) |
| `Game/enemy/Enemy.cpp` | Clip (753), Clip (810) |
| `Game/enemy/Enemy.h` | EnemyRenderAsset (11), EnemyPartVisual (16), DetachedEnemyPart (22), FaceShard (29), DebugDetached (79), DebugState (80) |
| `Game/enemy/EnemyAI.h` | EnemyAISettings (35), EnemyAI (45), Candidate (86) |
| `Game/enemy/EnemyAsset.h` | EnemyAsset (25) |
| `Game/enemy/EnemyDefinition.h` | EnemyTypeMarker (22), EnemyDefinition (28) |
| `Game/enemy/EnemyExplosion.h` | EnemyExplosion (6), EnemyBulletHitResult (12) |
| `Game/enemy/EnemyNavigation.h` | EnemyNavigation (10), Node (78) |
| `Game/enemy/EnemyObservationMemory.h` | EnemyObservationMemory (8), Sample (9) |
| `Game/enemy/EnemyParts.h` | EnemyPartVertex (51), EnemyPartGeometry (52), EnemyHpGroup (66), EnemyPart (86), EnemyParts (113), EnemyPartHit (153) |
| `Game/enemy/EnemyPlacements.h` | EnemyPlacement (15), EnemyPlacementGroup (21) |
| `Game/enemy/EnemyPool.h` | EnemyPoolSettings (4), Slot (85), Bucket (86) |
| `Game/enemy/EnemyProceduralAnimation.h` | EnemyProceduralAnimationSettings (8), RenderTransform (23), Pose (24) |
| `Game/enemy/EnemyProjectile.h` | EnemyProjectile (8) |
| `Game/enemy/EnemySenses.h` | EnemySenses (5) |
| `Game/enemy/EnemySpawnDebug.h` | Clip (13) |
| `Game/enemy/EnemySpawnSystem.h` | EnemyPoolEntry (15), EnemySpawnCount (16), EnemySpawnPoint (17), SpawnGroupEnemy (40), SpawnGroup (45), EnemySpawnTrigger (53) |
| `Game/GameOverSequence.h` | PostSettings (14), Settings (39), DeathContext (60), VisionState (86) |
| `Game/player/FPSMotion.h` | Settings (8) |
| `Game/player/Player.h` | DebugState (48) |
| `Game/PlayerVisualSettings.h` | PlayerVisualSettings (17) |
| `Game/scene/Debug/EnemyPlacementEditorScene.cpp` | Marker (738) |
| `Game/scene/Debug/EnemyPlacementEditorScene.h` | Preview (102) |
| `Game/scene/Debug/EnemyPlacementMetadata.h` | EnemyPlacementMetadata (8), Tag (9) |
| `Game/scene/Flow/TitleScene.cpp` | Glyph (165) |
| `Game/scene/Flow/TitleScene.h` | StartExplosionSettings (70), TextExplosion (78), LampFlickerState (108) |
| `Game/scene/Main/GameScene.h` | DebugFrame (107), ShutterVisual (149) |
| `Game/StageProgress.h` | GoalTrigger (13) |
| `Game/StageShutter.h` | ShutterProgressEvent (7), ShutterDefinition (8), ShutterEvent (18), StageShutter (23) |
| `Game/StageWorld.h` | StageCollider (14), StageHit (27), PlayerMotion (31) |
| `Game/TitleLighting.h` | TitleLighting (9), Flicker (10), Spot (17), Shadow (27) |
| `Game/WeaponEditor.h` | Entry (12), Limit (99) |
| `Game/WeaponSystem.h` | WeaponDefinition (60), WeaponPoolEntry (249), WeaponSpawnFilter (250), WeaponSpawnPoint (260), WeaponPickup (267), WeaponRandomSettings (275) |
| `Engine/2D/Sprite.h` | TransformationMatrix (13), SpriteTransform (18), Material (89), VertexData (99) |
| `Engine/2D/SpriteCommon.h` | MaskCB (52) |
| `Engine/2D/TextureManager.h` | PreparedTexture (15), TextureData (53) |
| `Engine/3D/Animation.h` | Keyframe (11), AnimationCurve (21), NodeAnimation (29), Animation (36) |
| `Engine/3D/Animator.h` | ManualJointTransform (16) |
| `Engine/3D/LightingParam.h` | LightingParam (4) |
| `Engine/3D/Model.cpp` | PmxMaterialRecord (1305), PmxVertexSkin (1312), PmxBoneRecord (1317) |
| `Engine/3D/Model.h` | VertexData (20), MaterialData (30), Joint (35), Skeleton (46), Node (53), NodeInstance (62), MeshData (67), VertexWeightData (84), JointWeightData (91), MeshInstance (98), ModelData (103), EmbeddedTexture (104), Material (126), MeshCollisionData (193), NodeRuntime (356), FlatNode (357) |
| `Engine/3D/ModelCache.h` | Writer (12), Reader (53) |
| `Engine/3D/ModelManager.cpp` | ComScope (69) |
| `Engine/3D/Object3d.h` | TransformationMatrix (31), CameraGPU (37), EffectParam (42) |
| `Engine/3D/Object3dLight.h` | DirectionalLight (9), PointLight (18), SpotLight (27) |
| `Engine/3D/Skeleton.h` | Joint (15) |
| `Engine/3D/SkinnedModel.h` | AnimKeyVec3 (16), AnimKeyQuat (21), Node (29), Bone (35), BoneAnimeChannel (42), AnimationClip (51), SkinVertex (60), MaterialCBData (68), TransformCBData (75) |
| `Engine/3D/SkinningTypes.h` | VertexInfluence (12), WellForGPU (18), SkinningInformation (23), SkinCluster (27) |
| `Engine/3D/Skybox.h` | TransformationMatrix (11), VertexData (15) |
| `Engine/3D/VideoPlayerMF.h` | XAudio2VoiceCallback (15) |
| `Engine/Audio/AudioSystem.cpp` | ChunkHeader (8), RiffHeader (13), FormatChunk (18) |
| `Engine/Audio/AudioSystem.h` | Sound (60), PlayingVoice (66) |
| `Engine/Core/D3DResourceLeakChecker.h` | D3DResourceLeakChecker (5) |
| `Engine/Core/FadeManager.h` | FadeSettings (9) |
| `Engine/Core/SrvManager.h` | Retired (49) |
| `Engine/DebugAI/BasicCombatDebugBot.h` | BehaviorPlan (16) |
| `Engine/DebugAI/DebugAIConfig.h` | DebugAIConfig (5) |
| `Engine/DebugAI/DebugAIManager.cpp` | ReplaySessionManifest (507) |
| `Engine/DebugAI/DebugAIManager.h` | DebugReplayObservationCheckpoint (23), HeldExternalAction (146) |
| `Engine/DebugAI/DebugAnomalyDetector.h` | DebugAnomalyFinding (13), Rule (40), RuntimeState (60) |
| `Engine/DebugAI/DebugGenericActionReplay.h` | DebugGenericReplayEvent (11) |
| `Engine/DebugAI/DebugInputReplay.h` | Header (37) |
| `Engine/DebugAI/DebugObservationEventRecorder.h` | RecentAction (36) |
| `Engine/DebugAI/DebugReplayPlayer.h` | DebugReplayAction (8), DebugReplayCheckpoint (17) |
| `Engine/DebugAI/DebugReplayRecorder.h` | DebugActionRecord (9) |
| `Engine/DebugAI/DebugTypes.h` | DebugAction (8), DebugAILoadingSourceFile (17), DebugMapBounds (24), DebugEntityState (30), DebugSpawnOverride (59), DebugGameState (66), DebugIssue (92) |
| `Engine/DebugAI/ImGui/DebugAIImGuiPanel.cpp` | DebugReplayLogEntry (51) |
| `Engine/DebugAI/ImGui/DebugAIImGuiPanel.h` | DebugAIImGuiPanelState (8), DebugAIImGuiPanelRequests (14) |
| `Engine/DebugAI/Protocol/DebugGenericTypes.h` | DebugVec3 (9), DebugEntity (25), DebugGenericAction (34), DebugObservation (64) |
| `Engine/DebugAI/Protocol/DebugProtocol.h` | DebugProtocolMessage (26) |
| `Engine/DebugAI/Tools/DebugAIViewer/AdapterDiagnostics.cpp` | SourceIntegrationIndex (119) |
| `Engine/DebugAI/Tools/DebugAIViewer/AdapterDiagnostics.h` | AdapterDiagnosticResult (9) |
| `Engine/DebugAI/Tools/DebugAIViewer/DebugAIViewer.cpp` | SemanticReviewEntry (163), ReplaySessionListEntry (178), ScenarioListEntry (186), ScenarioBatchItemResult (193), AutomaticReplayVerificationResult (209), ProjectScanResult (363), SourceEvidence (364), SourceFileSummary (372), SignalCandidate (401), AttackRangeCandidate (411), ActionSemanticInference (423), Pattern (503), LocalPolicyConfig (3875), Condition (3876), Rule (3883), LocalPolicyState (4045), PendingAttackSample (4056), BossLocalPolicyState (4470) |
| `Engine/DebugAI/Tools/DebugAIViewer/GameWindowCapture.cpp` | WindowSearch (10) |
| `Engine/DebugAI/Tools/DebugAIViewer/GameWindowCapture.h` | GameWindowCaptureResult (10) |
| `Engine/DebugAI/Tools/DebugAIViewer/ScenarioRunner.h` | Goal (61), Evidence (76) |
| `Engine/DebugAI/Tools/DebugAIViewer/TestReportGenerator.h` | TestReportGenerationResult (6) |
| `Engine/Effect/EffectManager.h` | EffectParticleNode (17), EffectReferenceNode (29), EffectObjectKeyframe (37), EffectObjectNode (50), EffectTemplate (70), ActiveEffectObject (78), ActiveEffect (95) |
| `Engine/Level/LevelLoader.h` | ObjectData (15), LevelData (35) |
| `Engine/math/AABB.h` | AABB (4) |
| `Engine/math/Matrix4x4.h` | Quat (66) |
| `Engine/math/Mesh.h` | Vertex (9), Mesh (18) |
| `Engine/math/Quaternion.h` | Quaternion (5), QuaternionTransform (12) |
| `Engine/math/Vector.h` | Vector3 (2), Vector4 (9), Vector2 (16), Transform (21) |
| `Engine/math/Vector3.h` | Vector2 (3), Vector3 (8), Transform (55), Vector4 (62) |
| `Engine/Particle/Particle.h` | ParticleForGPU (32), DirectionalLight (38), ParticleData (44), Emitter (55), AccelerationField (62) |
| `Engine/Particle/ParticleManager.cpp` | DummyMaterial (189), DummyLight (198) |
| `Engine/Particle/ParticleManager.h` | Particles (20), PerView (32), PerFrame (38), EmitterData (44), ParticleGroup (96), ParticleVertex (227), RetiredParticleGroups (237), MaterialForGPU (263) |
| `Engine/Physics/ClothComponent.h` | Chain (9), Group (10), ColliderBinding (11) |
| `Engine/Physics/ClothSolver.h` | PhysicsParticle (6), PhysicsConstraint (12), PhysicsCollider (18), PhysicsCollisionSegment (28), PhysicsCollisionSample (34), Settings (45) |
| `Engine/Render/DeathPostEffect.h` | DeathPostEffectParameters (7) |
| `Engine/Render/RenderManager.h` | BloomParameter (38), GaussianFilterParameter (171), OutlineParameter (178), RadialBlurParameter (191), DissolveParameter (203), RandomParameter (218) |
| `Engine/Render/ViewPostEffects.h` | DoubleVisionSettings (5), ImpactBlurSettings (12), ViewPostEffectParameters (16) |
| `Engine/Utility/AssetLoadProfile.h` | Event (17) |
| `Engine/Utility/AssetPreparation.h` | ComScope (11) |
