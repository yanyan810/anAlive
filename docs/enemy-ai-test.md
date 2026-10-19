# 敵AI改善と Enemy AI Test

## 実装内容

通常ゲームとテストシーンの双方が `Enemy::Update` → `EnemyAI::Update` を使用します。武器・銃の定義、銃弾の処理、敵種ごとのHP・速度・攻撃距離・ダメージ・間隔は変更していません。Debug Showroomと従来のDebugAITestSceneも残しています。着手時のGit作業ツリーに未コミット変更はありませんでした。

- 初期状態は待機。視認距離、水平視野角、頭／胴体の2点への視線、連続視認時間で発見します。
- 敵の目は移動Collider高さの85%の位置です。前方は既存モデルの向きに合わせて `rotation.y - π/2` から求めます。
- `StageWorld::Raycast` のOBB判定を再利用します。床・壁・開閉中のシャッターなど、その時点のColliderで遮蔽します。既存の `playerOnly` Colliderは既存のRaycast規約どおり除外します。
- 追跡中に視認した座標だけを最終目撃位置に保存します。見失い猶予中も、視認していない現在座標は移動先に渡しません。
- 待機 → 追跡／攻撃 → 最終目撃地点への移動 → 周辺探索（Search）→ 徘徊。再発見で追跡を再開します。出現地点への帰還処理はありません。
- 攻撃の既存処理を維持し、発見済み・現在視認中・移動Colliderの通行確認を追加しています。見失い猶予中も攻撃は止まります。遠距離型の後退も壁と床を確認します。既存の爆発ダメージ／爆風範囲の仕様は維持しています。
- 同じ高さの床で、直線移動を優先する限定的なA*を追加しました。65×65セル、展開上限1800、セル幅は敵半径に応じて最低0.75です。経路の各区間を、既存の円柱対OBB移動判定と同じ投影方法で確認します。
- 経路がない場合は停止し、おおむね1秒間隔で再試行します。Search終了後の徘徊先は、その時点の現在地の周囲から選び、壁内・床のない地点・到達できない地点を除外し、移動後に待機を挟みます。

## 起動と操作

タイトル左下の「F6 : Enemy AI Test（敵AIテスト）」表示に従ってF6を押します。Debug／Developmentではタイトル右側の同名ボタンも利用できます。既存のGameStart射撃演出とは独立しています。

リポジトリのプロジェクトディレクトリを作業ディレクトリにし、実行ファイルに `--enemy-ai-test` を指定して直接起動することもできます。シーン名は `EnemyAITest` です。

| 操作 | 内容 |
| --- | --- |
| P | 全AIの停止／再開。入場直後は停止しています |
| Tab | UI操作／FPS操作の切り替え |
| Esc | カーソル解放 |
| WASD／マウス | 既存PlayerのFPS移動／視点操作 |
| F5 | 全敵のAI状態・認識履歴・経路をリセット |
| F6 | タイトルへ戻る（タイトル上ではテストシーンへ移動） |

日本語ImGui画面は既存構成どおりDebug／Developmentで使用します。Releaseでも専用シーン、画面内の日本語操作説明、FPS操作、P／F5／F6は利用できます。ReleaseにImGuiを追加する全体設定変更は行っていません。

シーンには視認距離、視野角、壁越し視認、追跡・見失い、徘徊、複数敵の6エリアを用意しました。UIからエリア選択・プレイヤー移動ができます。距離エリアの床線は5単位間隔です。出現時は敵が手前（-Z）を向きます。

UIでは敵の定義選択、座標指定出現、選択削除／全削除、個別／全体停止、AIリセット、索敵・探索・記憶17項目の調整、状態確認、選択敵の3D表示切り替えができます。プレイヤーは初期設定で無敵です。攻撃テスト時は解除でき、体力回復ボタンもあります。銃の調整や射撃試験はこのシーンの範囲外です。

AIリセットは現在位置で待機に戻し、調整値は維持します。再出現はプールから取得し直すため、認識履歴・経路・停止状態・索敵調整値は定義の初期値に戻ります。停止中の視認情報は直前のAI更新時点の情報です。

## データ

`resources/Data/enemies.json` の各敵に任意の `senses` オブジェクトを追加しました。

```json
"senses": {
  "viewDistance": 20,
  "fieldOfView": 110,
  "detectionTime": 0.35,
  "loseSightTime": 2.0,
  "arrivalDistance": 0.6,
  "wanderRadius": 6,
  "wanderSpeedMultiplier": 0.45,
  "wanderWaitTime": 1.5,
  "investigateSpeedMultiplier": 1.0,
  "searchDuration": 4.0,
  "searchRadius": 5.0,
  "searchSpeedMultiplier": 0.85,
  "investigateUnreachableTime": 6.0,
  "memoryRetentionTime": 6.0,
  "memoryRecordInterval": 0.25,
  "memoryRecordDistance": 0.5,
  "searchDirectionPriority": 0.85
}
```

距離・速度は既存ゲームのワールド単位、時間は秒、視野角は水平の全角（度）です。`senses` がない場合も読み込めます。`viewDistance` の省略時は従来の `detectionRange` を使い、他は上記の初期値を使います。今回のデータでは敵種ごとの従来の `detectionRange` を視認距離に引き継いでいます。不正値の読み込み失敗時は、それまでの定義を維持します。

UIの編集は選択中のインスタンスへ即時反映し、JSONへの保存は行いません。「定義の索敵設定に戻す」でその敵の初期値へ戻せます。透明物への拡張用に、通常のステージColliderに任意の `blocksSight` を用意しました。省略時は `true`、`false` は視線だけを通し、物理移動・銃弾の判定は維持します。

## 変更ファイル

| 範囲 | ファイル |
| --- | --- |
| 共有AI／定義 | `Game/enemy/EnemyAI.h`, `EnemySenses.h`（新規）, `EnemyNavigation.h`（新規）, `EnemyDefinition.h`, `Enemy.h`, `Enemy.cpp` |
| プール | `Game/enemy/EnemyPool.h`（GPU待機後の明示的なテスト用削除API） |
| 当たり判定／読込 | `Game/StageWorld.h`, `Game/StageLoader.h`（視線用の任意フラグ。既定の物理判定は維持） |
| 専用シーン | `Game/scene/Debug/EnemyAITestScene.h`, `EnemyAITestScene.cpp`（新規） |
| 通常ゲーム | `Game/scene/Main/GameScene.cpp`（共有AIへStageWorldとプレイヤー目線高さを渡す）, `GameScene.h`（実機テストのfriend） |
| タイトル | `Game/scene/Flow/TitleScene.h`, `TitleScene.cpp` |
| シーン登録／描画 | `Engine/Core/GameApp.cpp`, `IScene.h`, `SceneManager.cpp`（専用シーンは汎用の英語フェードデバッグ窓を非表示） |
| プロジェクト登録 | `CG2_Setup.vcxproj`, `CG2_Setup.vcxproj.filters` |
| データ／説明画像 | `resources/Data/enemies.json`, `resources/ui/enemy_ai_entry.png`, `enemy_ai_help.png`（画像2点は新規） |
| 新規テスト | `tests/EnemySensesTests.cpp`, `EnemyAITestSceneTests.h`, `EnemyAIStageSmokeTests.h` |
| 既存テスト調整 | `tests/EnemyPartsTests.cpp`, `EnemyTypesTests.cpp`, `DebugToolsTests.cpp`, `EnemyPoolTests.h`（攻撃間隔／アニメーションなどの既存テストでは視野360度・発見時間0を明示。索敵条件は新規テストで検証） |
| 新規ツール | `tools/test-enemy-senses.ps1`, `test-enemy-ai-scene.ps1`, `test-enemy-ai-stages.ps1`, `generate-enemy-ai-ui.ps1` |
| 文書 | `docs/enemy-ai-test.md`（本書） |

## 検証結果（2026-10-19）

Debug x64／Release x64ともビルド成功しました。

新規の索敵テストは成功しています。実際のOBB壁、回転壁、複数視認点、視野・距離・時間、視認中断、見失い猶予、記憶位置の固定、到着後の徘徊、再発見、待機、近接／遠距離攻撃の遮蔽、後退方向の壁、迂回経路、経路なし、床の欠落、高低差の未対応表示、旧JSON互換、不正値を確認しました。

実際のD3Dを使用する専用シーンテストはDebug／Releaseとも成功しています。タイトルとのフェード往復、初期8体、定義別出現、即時の調整値反映、個別／全体停止、AIリセット、選択／全削除、同一プールスロットの再利用、再出現時の記憶／調整値の初期化、壁内出現拒否を確認しました。

Stage01〜03はそれぞれ別プロセスで、現在のステージJSON読み込み、180回の通常ゲーム更新、敵AIとプール／出現処理、有限座標、D3D描画を確認しました。Stage03はシャッターの開放・Collider更新・描画も確認しました。Debug／Releaseとも成功しています。ステージの通しプレイ完了を保証するテストではありません。

既存の敵パーツ、敵種、Debugツール、StageLoader、Showroom、StageProgress、EnemySpawn、FPS移動、Raycast、銃弾、実機EnemyPool、実機画面遷移、実機タイトル演出テストも成功しました。

| 要求された確認項目 | 結果 |
| --- | --- |
| 1〜3 壁・視野角・視認時間 | 自動テスト成功 |
| 4〜5 最終目撃位置へ移動／壁越しの現在位置追跡防止 | 自動テスト成功 |
| 6〜8 到着後徘徊・再発見・出現地点へ帰還しない | 自動テスト成功。AIは帰還用の出現座標を保持しません |
| 9 壁越し攻撃防止 | 近接・遠距離の自動テスト成功。爆風の仕様は従来どおり |
| 10〜11 タイトルとの往復 | Debug／Releaseの実機フェードテスト成功。日本語UIの戻るボタンもクリックして確認。F6の実キー入力は未確認 |
| 12 ImGui反映 | 実機テストでUIが参照する索敵値の変更と動作への反映を確認。スライダーのマウス操作は未確認 |
| 13〜14 削除・再出現・リセット・プール再利用 | 実機テスト成功 |
| 15 Stage01〜03維持 | 現行データで起動・更新・描画成功。全ルートの通しプレイは未検証 |
| 16 Debug／Releaseビルド | ともに成功 |

Windows画面操作では日本語UI、エリア映像、タイトル復帰ボタン、タイトルの追加項目を確認しました。その後、ユーザーの物理Escによってcomputer-useが停止されたため、最終の背景修正・初回停止表示の再目視確認、Tab／P／F5／F6の実キー入力、スライダーの実マウス操作は未完了です。以降の検証は自動テストのみです。

### 既存テストの不一致

`tools/test-hotel-stage.ps1` は `Six reserved spawns, no active schedule` で失敗しました。変更していない `tests/HotelStageChecks.h:98` は「出現点6個・稼働グループなし」を要求しますが、変更していない現行Stage03データにはStageStartの出現設定があります。今回のAI変更に合わせてステージデータやこの旧テストを書き換えることはしていません。上記の現行データを用いたスモークテストを別途追加しています。

## 再実行

プロジェクトディレクトリで以下を実行します。

```powershell
./tools/build.ps1 -Configuration Debug
./tools/build.ps1 -Configuration Release
./tools/test-enemy-senses.ps1
./tools/test-enemy-search.ps1
./tools/test-enemy-memory.ps1
./tools/test-enemy-ai-scene.ps1 -Configuration Debug
./tools/test-enemy-ai-scene.ps1 -Configuration Release
./tools/test-enemy-ai-stages.ps1 -Configuration Debug
./tools/test-enemy-ai-stages.ps1 -Configuration Release
```

専用の実機結果は `generated/enemy-ai-tests/` に出力します。ビルド済み実行ファイルはプロジェクトの親の `generated/outputs/{Debug,Release}/CG2_Setup.exe` です。リポジトリのresourcesを参照するため、起動時の作業ディレクトリはプロジェクトにしてください。

## 残る課題

- 経路探索は平面限定です。階段・段差・上下階・ジャンプ・移動する床・狭すぎる通路は未対応で、安全停止と状態表示を行います。大型敵では保守的な円柱対OBB判定によって通路を通れない場合があります。
- 迂回が探索範囲や1800展開を超える場合は上限として表示します。多数の敵が同時に経路再探索する大規模負荷試験は未実施です。3D表示は専用シーンの選択敵だけに限定しています。
- 通常ステージの全ルート・全戦闘、実キー操作と最終UIの目視確認、および旧ホテルテストと現行出現データの整合性確認が残ります。

## 背景のカメラ追従修正

背景が固定される報告を受け、静的な床・壁・障害物の `Object3d::Update` が生成時にしか呼ばれず、カメラ依存のWVP行列が古いまま残る問題を修正しました。プレイヤーのカメラ更新後に、背景の描画行列も毎フレーム更新します。全AI停止中にも反映します。

専用シーンの実機テストに背景だけのGPU画像比較を追加しました。修正前はカメラ回転後も画像が変わらず失敗し、修正後は回転・平行移動で画像が変わり、元のカメラに戻すと同じ画像へ戻ることを確認します。比較画像は `generated/enemy-ai-tests/camera-*.png` に出力します。

## 見失い後の追跡・周辺探索の強化

以前の実装および背景修正を含む未コミット変更を保持して追加しました。今回の変更対象は共有AI、既存経路の移動処理、索敵定義とJSON、専用シーン、関連テストと本書です。武器・敵HP・攻撃性能・アニメーション・EnemySpawn・StageWorldは今回変更していません。

### 行動

視認を失うと、最後に視認した座標だけを追跡します。猶予は既定で2秒。ChaseからInvestigateへ切り替わる際も経路と再試行間隔を維持します。Chase中は追跡速度、Investigate中は追跡速度×`investigateSpeedMultiplier`（既定1.0）で進みます。A*の細かい中継点に着いた際も、同じフレームの残り移動量を次の区間に使い、経路の点の数によって遅くならないようにしました。

到着判定は既存の距離と同じ床高さの条件で行います。到着前にはSearchの時間を進めません。猶予中に先に到着した場合も、猶予が終わるまではChaseとしてその地点を確認し、Investigateを経てSearchを開始します。

Searchは既定4秒、半径5、追跡速度の0.85倍です。最終目撃位置を中心とする範囲で、現在地から短い移動先を選び、既存A*と円柱／OBB・床判定で到達できる場所を順番に確認します。壁内・床のない候補は除外します。終了時には現在の安全な経路区間を終えてからWanderへ移り、長い迂回経路を最後まで追い続けることや、移動途中の急停止を避けます。Wanderは後述の履歴追跡改善により、Search終了時の現在地を中心に、0.45倍の速度と短い待機を繰り返します。

経路なし・探索上限・高低差などでInvestigateの経路失敗が既定6秒連続した場合は、到達不能を明示して現在地周辺のSearchに切り替えます。この場合に限り探索中心は到達可能な現在地とし、その後のWanderはSearch終了地点の周囲で行います。最終目撃位置は上書きせず、元の失敗理由も保持します。通常の到着と到達不能による代替行動はUIで区別できます。いずれも出現地点への帰還ではありません。

新規JSON項目の省略時は上記の既定値を使用します。既存JSONに明示された旧値は尊重します。今回の`enemies.json`では各敵の猶予を2秒へ更新しています。UIの探索時間・速度倍率・半径変更も実行中に反映され、探索範囲を小さくした場合には範囲外の目的地を選び直します。JSON保存は引き続き行いません。

### 表示と操作

既存のP、Tab、Esc、F5、F6操作はそのままです。「追跡・見失い」エリアを選び「このエリアへ移動」、PでAIを再開してください。敵の正面から姿を見せ、右の縦壁の南端（Z=20付近）を右へ曲がると遮蔽を確認できます。右端を回って北へ進むとL字壁の反対側へ移動できます。エリアは初期位置から敵へ視線が通るL字壁へ調整しました。

追加表示は見失い経過時間、最終目撃地点への到着／到達不能、Search残り時間・確認済み地点数・現在の探索先、経路失敗の継続時間・理由・探索試行回数、実移動速度です。停止中の速度は0と表示します。

3D表示は橙が最終目撃位置、紫が追跡／Investigateの移動先、水色がSearchの移動先、緑が経路です。重なるマーカーのラベルは高さをずらして区別します。

今回の追加・拡張ファイルは以下です。

- `Game/enemy/EnemyAI.h`, `EnemySenses.h`, `EnemyNavigation.h`, `EnemyDefinition.h`
- `resources/Data/enemies.json`
- `Game/scene/Debug/EnemyAITestScene.cpp`
- `tests/EnemySearchTests.cpp`（新規）, `tests/EnemySensesTests.cpp`, `tests/EnemyAITestSceneTests.h`, `tests/EnemyPoolTests.h`
- `tools/test-enemy-search.ps1`（新規）, `docs/enemy-ai-test.md`

### 追加検証の範囲

`test-enemy-search.ps1`は共有AIを使い、速度維持、猶予／経路維持、透視防止、到着後だけのSearchタイマー、複数地点、時間切れの移動継続、再発見、壁・床の候補除外、旧JSON、停止・複数回の再探索・代替行動・障害物解除後の再開を確認します。

`test-enemy-ai-scene.ps1`には、専用シーンの実際のL字壁とプール内の敵を使った一連の確認を追加しています。Searchの途中でAIをリセットする場合と、Searchの記憶・経路・調整値が残った敵を削除して同一スロットへ再出現する場合も確認します。

手動の通しプレイ・マウスでの各新規スライダー操作・多数同時探索の負荷測定は今回未実施です。平面経路探索の制約と、旧ホテルテストの出現設定の前提不一致は残ります。

追加検証の実行結果は次のとおりです。

| 確認 | 結果 |
| --- | --- |
| Debug／Release x64ビルド | 両方成功 |
| 共有AIのSearch／索敵テスト | 成功。猶予中の到着、速度倍率変更、探索半径縮小、未発見時のIdle維持も確認 |
| 専用シーン実機テスト | Debug／Releaseとも成功。L字壁での追跡→Investigate→複数地点Search→Wander→再発見、壁越し座標の非更新を確認 |
| Search状態のリセット／プール再利用 | 実機で成功。記憶・経路・タイマー・確認数・代替探索フラグ・調整値の初期化を確認 |
| 既存の敵パーツ／攻撃間隔・敵種・Debug履歴テスト | 成功 |
| 既存EnemyPool／EnemySpawn／Debug Showroom実機テスト | 成功 |
| Stage01〜03の現行データ | Debug／Releaseとも読み込み、各180回の通常更新、D3D描画が成功。Stage03のシャッター開放も確認 |
| 背景カメラ反映・タイトル往復 | 専用シーン実機テストで引き続き成功 |

実機結果は `generated/enemy-ai-tests/scene-result-{Debug,Release}.txt` と `stage0*-smoke-{Debug,Release}.txt` に保存します。ステージ確認は起動・更新・描画のスモークテストであり、全ルートの戦闘・クリアまでの通しプレイは含みません。

## 視認した移動履歴と探索方向の改善

### 記憶する情報

共有EnemyAIに `EnemyObservationMemory` を追加しました。視認距離・視野角・実際のCollider遮蔽を通った座標だけを記録します。既定では0.25秒または0.5単位の移動ごとに記録し、直近6秒、最大64点です。各点は座標・シミュレーション時刻・連続視認区間を持ちます。最初の発見時間を満たす前でも、実際に視認できている座標は記録します。AI停止中は記憶の時計も停止します。

見失っている間は古い点の期限切れ処理だけを行い、未視認のプレイヤー位置を追加したり、そこから逃走方向を更新したりしません。遮蔽後の再発見は別の視認区間として記録するため、見えていなかった区間を直線で補って逃走方向を推定することもありません。

方向は直近の連続視認区間で、最大1.5秒・おおむね最後の0.5単位の移動差から求めます。微小な揺れだけの場合は方向なしとし、履歴が足りない場合は従来の周辺探索を使います。見えている間に曲がった場合は、古い進行方向より最後の移動方向を優先します。

位置の履歴が期限切れになっても、今回のSearchでは最後に視認したときの推定方向を固定して使用できます。これは未視認位置からの再計算ではありません。保持時間を0にした場合は位置履歴と推定方向の両方を無効化します。再発見後は新しい連続視認区間から再推定します。

AIリセットとEnemyPoolの再出現では、座標・時計・区間・推定方向を消去します。Debugの巻き戻しは、そのフレームで持っていた履歴と方向を復元します。

### 追跡・候補選択

最終目撃地点へは従来どおり追跡速度で移動します。履歴を順番にたどる移動は追加していません。最終目撃地点への経路は既存のA*を使用し、到着後に逃走方向を優先したSearchを行います。

探索先は現在地を基準に、前方・左右・斜め・後方と距離の異なる最大16候補から選びます。推定方向との一致度、前方への進行、確認済み地点への戻りにくさ、設定した方向優先度で候補を評価します。壁内・床のない地点は先に除外し、その後にA*で到達を確認します。著しく長い迂回も却下し、短い探索のために大きく往復しないようにしています。

計算量を抑えるため、A*は1回の候補選択で最大6候補まで試します。失敗した場合は0.5秒後に別の候補群へ進め、優先度の低い到達可能地点も確認します。前方が塞がれた場合に、同じ失敗候補だけを繰り返して側方・後方の代替地点を確認できなくなることを避けます。候補がなければ安全停止してSearchの間は再試行し、即座にWanderへ移ることはありません。

Search中に再発見した場合、初回の発見時間をもう一度待たずChaseへ戻ります。攻撃距離・ダメージ・間隔と遮蔽条件は維持しています。Search終了後のWanderは、その終了時の現在位置を中心にします。出現地点や古い観測点へ戻る処理はありません。

### 設定・表示

敵定義の任意の`senses`項目に以下を追加しました。旧JSONで省略されていても動作します。

| 項目 | 既定値 | 用途 |
| --- | --- | --- |
| `memoryRetentionTime` | 6秒 | 視認した座標の保持時間。0は記憶を無効化 |
| `memoryRecordInterval` | 0.25秒 | 記録間隔 |
| `memoryRecordDistance` | 0.5 | 時間を待たず記録する移動距離 |
| `searchDirectionPriority` | 0.85 | 0で従来の周辺探索、1で観測方向を強く優先 |

Enemy AI Testには4項目の調整、記憶座標一覧（経過秒・座標・視認区間）、推定逃走方向、現在のSearch先、却下候補数・座標・理由を追加しました。3D表示の青線／点は視認した履歴、赤矢印は最終視認時の逃走方向です。遮蔽を挟む視認区間の間は青線を結びません。現在の移動経路は従来の緑、Search先は水色のままです。各表示は個別に切り替えられます。

経路失敗は、開始地点の障害物との重なり、目的地の壁内、高低差、床なし、探索範囲外、計算上限、経路が接続していない、移動区間の閉塞、長すぎる迂回、有効候補なしを日本語で区別します。到達不能から代替Searchへ進んだ場合も、元のInvestigateの理由を保持します。

### 今回の変更ファイル

- `Game/enemy/EnemyObservationMemory.h`（新規）、`EnemyAI.h`、`EnemySenses.h`、`EnemyDefinition.h`、`EnemyNavigation.h`
- `resources/Data/enemies.json`（記憶と方向優先度だけ追加）
- `Game/scene/Debug/EnemyAITestScene.h`、`EnemyAITestScene.cpp`
- `CG2_Setup.vcxproj`、`CG2_Setup.vcxproj.filters`（新規ヘッダー登録）
- `tests/EnemyMemoryTests.cpp`（新規）、`EnemyAITestSceneTests.h`、`EnemyPoolTests.h`、`EnemySensesTests.cpp`、`EnemySearchTests.cpp`
- `tools/test-enemy-memory.ps1`（新規）、`docs/enemy-ai-test.md`

既存の未コミット変更、背景カメラ修正、タイトル遷移、EnemySpawn、StageWorld、武器・HP・攻撃性能・アニメーションは維持しています。

### 検証する動作と制約

新規の`test-enemy-memory.ps1`は、時間／距離での記録、64点上限、期限切れ、未視認座標の非追加、視野角外の非記録、遮蔽区間の分離、最後に視認した曲がり角方向、方向付きSearchと側方候補、失敗候補の後にも後方の到達可能候補を試すこと、安全停止、現在地中心のWander、各種A*失敗理由、旧JSONを検証します。

実機シーンテストは、実際のL字壁とプール内の敵で、視認中の横移動→方向推定→遮蔽→未視認座標を記録せず最終目撃地点へ追跡→逃走方向の到達可能地点をSearch→現在地中心Wander→再発見を確認します。リセット・同一プールスロット再利用で記憶と設定を初期化し、背景カメラ・タイトル往復の既存確認も維持します。

手動での全ステージ通しプレイ、新規UIのマウス操作、多数の敵が同時に探索する負荷測定は未実施です。経路探索は引き続き平面限定で、階段・上下階は未対応です。旧ホテルテストと現行Stage03の出現設定の前提不一致は今回変更していません。

今回の実行結果は、記憶・Search・索敵・敵パーツ・敵種・Debug履歴のCPUテストが成功、EnemyPool／Debug Showroomの実機テストが成功、Debug／Releaseのビルド・L字壁の実機シーンテスト・Stage01〜03の各180回更新とD3D描画がすべて成功です。Stage03ではシャッター開放も確認しています。`enemies.json`の`senses`以外の全項目はGit HEADと一致することも確認しました。

履歴の初期化テストでは、Acquire／AIリセット直後の記憶時計0と、再出現後に通常更新した分だけ進む時計を区別します。見えていない相手を3秒更新した場合でも時計は3秒へ進みますが、位置履歴は空のままです。巻き戻しでは、保存フレームの履歴・時計・方向の復元を確認しています。

再実行は`./tools/test-enemy-memory.ps1`、`./tools/test-enemy-ai-scene.ps1 -Configuration Debug`（または`Release`）、`./tools/test-enemy-ai-stages.ps1 -Configuration Debug`（または`Release`）です。実機の構成別結果は`generated/enemy-ai-tests/scene-result-{Debug,Release}.txt`、`stage0*-smoke-{Debug,Release}.txt`に保存します。
