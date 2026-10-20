# Enemy Placement Editor 実装・検証報告

## 対象と既存機能の再利用

`EnemyPlacementEditorScene` を独立シーンとして追加しました。初期ステージは Stage03 です。ステージ選択では `resources/levels/<id>/<id>.json` のあるステージを利用できます。Stage01・Stage02にも同じ実装を使います。

- ステージ読み込み・Collider：`StageLoader` / `StageWorld`。
- FPSの移動・視点・プレイヤーの高さ：既存の `Player`。
- 出現条件・種類別体数・順次／同時・初期遅延・出現間隔・Max Alive：`EnemySpawnSystem`。
- 敵モデル・サイズ・向き・部位モデル：`Enemy` と既存の共有アセットキャッシュ。編集中は専用の静止インスタンスを使用し、EnemyPoolには登録せず、AI更新・攻撃処理を呼びません。
- テスト：既存の `GameScene`、`EnemyPool`、戦闘・AI・当たり判定を使用します。未保存の配置データのコピーをGameSceneへ渡します。
- タイトルとの遷移：既存の `SceneManager::TransitionTo` とフェード。
- UI：既存のImGuiワークスペース／Sceneプレビュー。Releaseでも専用エディターのUIを使用でき、通常ゲームのRelease表示は全画面のままです。

敵AI、武器、攻撃、HP、部位破壊、アニメーションの仕様や調整値は変更していません。Blenderアドオンと既存のStage03モデル・JSON・blendも編集していません。開始時の未コミット変更と、作業中に外部から変更されたStage03アセットを保持しています。

## 操作

タイトルの `Enemy Placement Editor` 項目、または **F7** で開きます。タイトル演出の開始中は遷移を受け付けません。Enemy AI TestのF6とは独立しています。直接起動する場合は、プロジェクトを作業ディレクトリとして実行ファイルへ `--enemy-placement-editor` を渡します。

| 操作 | 内容 |
|---|---|
| Tab | UI操作／カメラ操作を切り替え |
| Esc | マウスを解放 |
| WASD | カメラ移動。FPSでは既存Playerによる移動 |
| Q / E | 自由・俯瞰カメラの上下移動 |
| マウス | カメラ操作中の視点回転 |
| Shift | 自由・俯瞰カメラの高速移動 |
| F / 「選択へカメラを移動」 | 選択した配置へ移動 |
| Alt＋視点操作 | 自由カメラで選択を中心に回転 |
| 俯瞰でホイール | 平行投影の表示範囲を変更 |
| 俯瞰で中ボタンドラッグ | 平行移動 |
| 一覧／画面のクリック | EnemySpawn・個別の敵を選択 |
| Ctrl＋クリック | 複数選択 |
| Shift＋左ドラッグ | 画面内の配置を範囲選択 |
| 色付きの軸先端をドラッグ | X/Y/Z方向の移動。複数選択もまとめて移動 |
| 回転ギズモの円をドラッグ | Yawを編集 |
| F6 / 「タイトルへ戻る」 | タイトルへ戻る。未保存時は確認 |
| F8 / 「編集へ戻る」 | テスト終了。編集データは保持 |

左側の設定欄はスクロールできます。設定と映像の幅は境界をドラッグして変更できます。位置・Yawの数値入力、グリッド・角度スナップ、床スナップ、位置／回転リセット、種類変更、有効／無効、追加／削除に対応しています。位置リセットは読み込み時または最後の保存時の相対位置へ戻します。新規の敵はEnemySpawn原点へ戻します。回転リセットは設定した初期方向へ戻します。

自由カメラと俯瞰カメラはColliderで移動を制限しません。FPSカメラは既存PlayerとColliderを使用します。FPS切替時に近傍の床・安全な位置を取得できなければ、前回のPlayer位置を使い、その旨を表示します。

## 階層・描画フィルター

Stage03の `stage03-layout.json` の `floorHeights: [0,6,12]` と部屋のboundsを使用します。階高をエンジンへ固定値として埋め込んでいません。

1F／2F／3F／全階、屋根、天井、上階、下階、壁、敵モデル、EnemySpawn、Collider、グリッドを切り替えられます。俯瞰へ切り替えると屋根・天井・上階を非表示にします。非表示にするのは `Object3d` インスタンスの描画メッシュだけです。共有Model、StageWorld、保存元のステージデータは変更しません。

モデルのメッシュと所有ノード名を対応させ、`F3_`、`Room301`、`Roof`、`Floor`、`Wall`、`Ceiling`などを分類します。未分類オブジェクトは表示を維持します。CollectionがglTFに含まれることは前提にしていません。

将来の明示メタデータは次の入口に対応しています。

1. glTFノードの `extras.enemyPlacementEditor` に `floor`（1始まり）、`kind`（roof/ceiling/wall/floor/other）、`room` を設定。
2. ステージフォルダーの `editor_metadata.json` に設定。ノードextrasより優先します。

```json
{
  "floors": [0, 6, 12],
  "nodes": {
    "Hotel_Roof_MainSlab.001": {"kind": "roof"},
    "F3_Room301_Floor.001": {"floor": 3, "kind": "floor", "room": "Room301"}
  },
  "spawns": {
    "enemySpawn.003": {"floor": 3, "room": "Room301"}
  }
}
```

Blenderアドオンの出力機能そのものは今回変更していません。このメタデータは任意です。

## 自動配置と個別配置

初回はBlenderの種類別体数から編集用の配置を作成します。元の有効な体数を維持し、床・壁・天井との問題は警告します。元のデータにない配置は、保存するまで通常ゲームへ適用されません。

「現在の体数を自動再配置」は、指定半径内で床と既存の衝突形状、他の敵との間隔を調べ、上限のある探索を行います。初期Yaw・ランダム方向を設定できます。配置できなかった敵は理由を表示して無効化します。再配置は明示的なボタン操作で行い、毎フレーム位置を変えることはありません。個別IDを保持したまま位置を再計算します。無効なモデルも編集用には表示されますが、実プレイの出現数には含みません。

Stage Start／On Trigger／Manualの独立EnemySpawnでは、**有効な個別配置数を実際の出現数に反映**します。種類変更・追加・削除・無効化に一貫して適用します。出現条件、初期遅延、Sequential／Simultaneous、Spawn Interval、Max Aliveは元の設定を維持します。Max Aliveに空きができたときは次の個別配置から出現します。

旧共有トリガー・旧SpawnGroupについては、共有総数と元の抽選を初期状態で維持するため、「このEnemySpawnの編集配置を使用」はオフです。オンにすると個別配置とEnemy Definitionを順番に使用し、配置数を超える共有出現は順番に繰り返します。共有トリガーの総数は変更しません。この違いはUIにも表示します。チェックがオフの間、配置編集とギズモを無効にします。

## 保存形式と座標

保存先は `resources/levels/<stage>/enemy_placements.json` です。`<stage>.json`、glTF、binには書き込みません。

```json
{
  "version": 1,
  "stage": "stage03",
  "coordinates": "engine-spawn-local-radians",
  "nextId": 4,
  "spawns": [{
    "spawnId": "enemySpawn.003",
    "mode": "Individual",
    "overrideEnabled": true,
    "radius": 3.0,
    "initialYaw": 0.0,
    "randomYaw": false,
    "enemies": [{
      "id": "placement-1",
      "enemy": "normal",
      "offset": [1.0, 0.0, 2.0],
      "yaw": 1.5707963,
      "enabled": true
    }]
  }]
}
```

個別IDは単調増加するカウンターで発行します。体数調整では末尾から対象種類の敵を削除し、残った敵のIDを割り当て直しません。`spawnId` が所属EnemySpawn IDです。

位置はスケールを含まないEnemySpawnの回転座標系に対する相対座標です。ワールド位置は `offset × spawnRotation + spawnPosition`、編集位置の保存はその逆行列を使います。Yawは `spawn.rotation.y + placement.yaw` です。敵は直立させ、Yawを編集します。UIではメートル・度を表示し、JSONではメートル・ラジアンを保存します。

Blender座標は既存アドオンの `ENGINE_BASIS` で変換済みのステージJSONを読みます。エディターで再変換しません。既存のEnemyAIと同じローカル-Xの正面方向を方向矢印にも使用します。EnemySpawnを移動・回転して再出力すると相対配置が追従します。

保存は次の順序です。

1. 保存内容の確認画面で、EnemySpawnごとの変更前／変更後と保存先を表示。
2. 数値・定義・ステージID・個別IDの重複を検証。
3. 外部変更がないことを確認。
4. 同じフォルダーの `.tmp` へ書き込み、flush、再読み込み・検証。
5. 既存ファイルを `.bak` にコピー。
6. Windowsの置換操作で本体を更新。

書き込み失敗時は元ファイルを保持します。外部変更時は上書きせず再読み込みを促します。再読み込み、ステージ変更、タイトルへ戻る、ウィンドウを閉じる場合は未保存の変更を確認します。元に戻すには「再読み込み・編集前へ戻す」を使います。前回保存分の復旧には `.bak` を利用できます。

EnemySpawn IDの削除・変更で参照先がなくなった配置は警告し、データを保持します。重複IDは既存StageLoaderと配置ローダーで検出します。ステージ形状の再出力後も配置検証を行い、床なし・壁／天井との重なり・敵同士の重なりを表示します。配置ファイルがない通常ステージは既存処理を使用します。通常ゲームで配置ファイルが破損していた場合も元の出現処理へ戻し、診断を残します。エディターでは破損ファイルの誤上書きを避けるため保存・テストを止めます。

## テストプレイ

「テストプレイ開始」で現在の編集データのコピーを既存GameSceneへ一時適用します。ディスクへの保存は不要です。ゲーム映像をクリックするとFPS操作に入り、Escで解放します。ManualのEnemySpawnはテスト画面の選択メニューから起動できます。On Triggerは通常と同じプレイヤー侵入で起動します。

F8または「編集へ戻る」でGPU完了を待ち、テスト用GameSceneとEnemyPool、弾・敵・追跡記憶を破棄します。編集カメラと静止プレビューを復元します。テスト中の敵の位置や向きは編集データへ書き戻しません。死亡・クリアでもエディターを終了せず、編集に戻って再テストできます。

## SRV上限エラーの修正

確認用ゲームの再読み込みテストで、ユーザーから報告された `SrvManager.cpp:27 / SRV index overflow` を再現しました。呼び出し元は `SyncPreviews → Enemy::Initialize → Object3d::SetModel → Animator::CreateSkinCluster` でした。

- 分割パーツを描画する静止プレビューでは、描画しない全身モデルのスキニング用SRVを確保しません。
- Animatorの再初期化・破棄時に4つのSRV/UAVを返却します。
- 返却時は関連GPUリソースを保持し、次の描画開始前にGPU完了を待って枠を再利用します。
- タイトルのシャドウマップもシーン破棄時にSRVを返却します。
- 512枠の上限は維持します。Releaseで上限を超えた場合も範囲外のハンドルを返さず、例外として通知します。
- 同一プロセスでStage03の再読み込みとテスト開始／終了を8回繰り返し、使用数と割当上限が増えないことを検証します。

## 主な変更ファイル

| ファイル | 変更 |
|---|---|
| `Game/scene/Debug/EnemyPlacementEditorScene.h/.cpp` | 専用シーン、UI、カメラ、選択・ギズモ、静止プレビュー、テスト切替 |
| `Game/scene/Debug/EnemyPlacementMetadata.h` | 階高・部屋・ノード分類、明示メタデータ |
| `Game/enemy/EnemyPlacements.h` | 配置形式、座標変換、検証、安全な保存 |
| `Game/enemy/EnemySpawnSystem.h/.cpp` | 配置の読み込みと共通出現処理への適用 |
| `Game/enemy/Enemy.h/.cpp` | 静止プレビュー用初期化・ハイライト |
| `Game/scene/Main/GameScene.h/.cpp` | 未保存データを使う埋め込みテスト、終了時の制御 |
| `Game/scene/Flow/TitleScene.h/.cpp` | F7・クリック・ImGuiのエディター入口 |
| `Engine/3D/Object3d.h/.cpp` | インスタンス単位のメッシュ描画フィルター |
| `Engine/Camera/Camera.h/.cpp` | D3D深度0～1に対応した任意の平行投影 |
| `Engine/3D/Animator.h/.cpp`, `Engine/Core/SrvManager.h/.cpp` | GPU完了後のディスクリプター返却・再利用 |
| `Engine/Render/DirectionalShadowMap.h` | シーン固有SRVの返却 |
| `Engine/Core/GameApp.cpp`, `Engine/Core/ImGuiManagaer.cpp` | シーン登録、起動引数、ReleaseのエディターUI |
| `Engine/Core/WinApp.h/.cpp` | エディターの未保存時の閉じる操作の確認 |
| `resources/ui/enemy_placement_entry.png` | タイトルのエディター入口表示 |
| `CG2_Setup.vcxproj/.filters`, `.gitignore` | ビルド登録、配置バックアップの除外 |
| `tests/EnemyPlacementTests.h`, `tests/EnemySpawnTests.cpp` | 配置形式・共通出現処理の回帰テスト |
| `tests/EnemyPlacementEditorTests.h` | 実D3D描画、復元・プレビュー・SRV再利用の統合テスト |
| `Tools/test-enemy-placement.ps1`, `Tools/test-enemy-spawn.ps1` | テスト実行 |

## 検証と未検証事項

2026年10月20日に以下を実行しました。

| コマンド | 結果 |
|---|---|
| `Tools/build.ps1 -Configuration Debug` | 成功、警告0・エラー0 |
| `Tools/build.ps1 -Configuration Release` | 成功、警告0・エラー0 |
| `Tools/test-enemy-spawn.ps1` | 成功。既存出現条件と新規配置形式・相対変換・保存・バックアップ・外部変更拒否・再出力保持・破損時フォールバック |
| `Tools/test-stage-loader.ps1` | 成功。ステージ／座標／OBB／既存出現・武器・Goalとの互換性 |
| `Tools/test-showroom.ps1` | 成功。アセット・種類・配置・武器・衝突の互換性 |
| `Tools/test-enemy-placement.ps1 -Configuration Debug` | 成功。実D3D描画、Stage01～03、未保存配置での本番GameSceneテスト、復元、SRV反復 |
| `Tools/test-enemy-placement.ps1 -Configuration Release` | 成功。同じ統合テストをReleaseで確認 |
| `Tools/test-enemy-ai-stages.ps1 -Configuration Debug` | Stage01～03すべて成功。各180フレームのAI・出現・当たり判定・描画、シャッターがある場合は開閉 |
| `Tools/test-enemy-ai-stages.ps1 -Configuration Release` | Stage01～03すべて成功 |
| `Tools/test-enemy-ai-scene.ps1 -Configuration Debug` | 成功。既存Enemy AI Testの視認・追跡・探索・再発見・プール復帰 |
| `Tools/test-enemy-pool.ps1` | 成功。実D3Dプール再利用、型・HP・部位・アニメーション・巻き戻し・Debug Showroomのリセット |
| `Tools/test-title-scene.ps1` | 成功。既存照明、文字破壊・演出、影、ステージ開始、黒画面・フェード |
| `git diff --check` | エラーなし |

SRVについてはDebug・Releaseとも8回の再読み込み／テスト切替後に **使用20、割当上限185、ヒープ容量512** で安定しました。結果は `generated/placement-tests/descriptors-Debug.txt` と `descriptors-Release.txt`、統合テスト結果は `result-Debug.txt` と `result-Release.txt` にあります。途中で検出したSRV枯渇と、新メニューがタイトル照明テストの採取範囲に重なる問題を修正してから再検証しました。タイトルのF7表示は既存F6表示と重ならない右下に配置しました。

依頼された確認項目に対する範囲は次のとおりです。「自動」は実際のローダー・スケジューラー・GameScene・GPU描画などを使ったコード上の確認を意味し、実マウス／キー操作の確認とは区別しています。

| 依頼項目 | 確認範囲 |
|---|---|
| 1・2：タイトルとの往復 | シーン登録とフェード往復を自動確認。F7・項目クリックの実操作は未検証 |
| 3～6：カメラ・UI入力 | 自由／FPS／平行投影の描画を自動確認。WASD・マウス操作、入力切替の操作感は未検証 |
| 7・8：Stage03・各階 | 読み込みと1～3Fのフィルター切替を自動確認 |
| 9・10・12：屋根・上階・Collider | 描画の差分、他インスタンスへの非干渉、Collider保持を自動確認 |
| 11：壁の非表示 | 実装済み。壁だけの切替に対する独立した画像比較・手動確認は未検証 |
| 13・14：選択 | 一覧・ID検索・レイ選択・範囲選択を実装。実クリック操作は未検証 |
| 15～18：位置・回転・種類・複数配置 | JSON・共有出現処理の複数種類と体数、実プレビューとGameSceneの位置・Yaw一致を自動確認。入力欄・ギズモの実操作は未検証 |
| 19：自動／個別配置 | 両方式・明示再生成を実装、Stage03初期配置の生成とプレビューを自動確認。全種類・全範囲の組合せは未検証 |
| 20：不正配置 | 壁との重なり・床なしを自動確認、現在のStage03の天井／屋根との重なりも警告 |
| 21～23：保存・再読み込み・再出力保持 | 実ファイルの保存／読み込み／バックアップと、ステージJSON再書き込み後の保持を自動確認。Blenderアプリの実操作は未検証 |
| 24：Stage Start | 実GameSceneに未保存の位置・Yawが反映されることを自動確認 |
| 25・26：On Trigger・Manual | 共通スケジューラーで位置・種類・有効数・順次／同時・間隔を自動確認。個々のステージの全トリガー通しプレイは未検証 |
| 27：旧ステージ | 配置ファイルなしのフォールバックとStage01～03の通常実行を自動確認 |
| 28～31：テスト・AI・編集復帰・データ分離 | 実GameScene、AI更新、停止・復帰、JSON比較による移動混入防止を自動確認 |
| 32：EnemyPool | 既存GPU再利用テストと、配置テストの反復を自動確認 |
| 33：通常ゲーム | Debug／ReleaseでStage01～03各180更新と実D3D描画を確認。全ステージの手動クリアは未検証 |
| 34・35：既存テストシーン | Enemy AI Test、Debug Showroomの既存回帰テスト成功 |
| 36：両構成のビルド | Debug／Releaseとも警告0・エラー0 |

Windowsの画面操作ツールは `CG2_Setup` へのアクセス許可を持たず、直接のUI操作確認を拒否しました。そのため、マウス／キーボードによる操作感、ギズモのドラッグ、狭い画面での使いやすさ、タイトル項目の実クリック、OSの閉じる確認ダイアログの実操作は未検証です。描画テストによるGPU画像は `generated/placement-tests/` に出力します。これらはゲーム自身のレンダーターゲットから取得した画像で、OS画面操作によるキャプチャではありません。

現在のStage03には、normalの当たり判定の高さ3.22mなどが屋根・開口部と重なる配置があります。屋根を非表示にしても衝突形状は残すため、警告はそのまま表示されます。自動配置が成功しない場合は配置範囲、位置、敵の種類を確認してください。この作業では元の敵の寸法やホテルの形状を変更していません。

Blender再出力の保持は、同じステージJSONを再書き込みする回帰テストで検証します。Blenderアプリからの実操作による再エクスポート、ステージ全体の通しプレイ、全種類・全条件を組み合わせた長時間の手動テストは未検証です。旧共有トリガーの配置は上述の順番繰り返し方式で、独立EnemySpawnの「有効な配置数＝総出現数」とは扱いが異なります。
