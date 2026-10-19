# EnemySpawn拡張の実装報告

実施日：2026年10月19日。操作手順・JSON例・API仕様は [enemy-spawn-activation.md](enemy-spawn-activation.md) にあります。

## 実装内容

EnemySpawnに固定の敵定義、ステージ開始／トリガー／手動起動、グループID、総数、間隔、初回待機時間、生存上限、同時／順次出現を追加しました。新形式Triggerはグループを起動する役割だけを持ち、各地点が独立して出現を進めます。

3種類の起動は同じ地点別出現処理に合流し、GameSceneの共通コールバックから既存EnemyPoolのAcquire／ResetForSpawnを使います。通常出現にEnemy::Initializeは追加していません。生存上限で待機した残数は保持し、死亡後に続行します。Stage Startの待機0秒はOnEnter内で準備します。遅延付きTriggerは低フレームレートで遅延の期限をまたいでも、期限を基準に地点側の待機・順次出現を計算します。

共通APIはEnemySpawnSystem::ActivateSpawn／ActivateGroup、シーンからの入口はGameScene::ActivateEnemySpawnです。Reset／再読込で出現状態を初期化し、デバッグ巻き戻しにも状態が含まれます。

## 変更ファイル

|ファイル|変更内容|
|---|---|
|Game/enemy/EnemySpawnSystem.h|独立設定と実行状態、共通起動API、共通出現ループ、Stage StartとReset。|
|Game/enemy/EnemySpawnSystem.cpp|新JSONの検証・読込、旧共有Triggerの互換読込、新旧混在の誤設定検出。|
|Game/scene/Main/GameScene.h|イベントから使う起動APIと共通出現更新。|
|Game/scene/Main/GameScene.cpp|既存プール取得経路の共通化、操作前のStage Start準備、日本語の出現デバッグ表示。|
|tools/blender/yanengine_level_exporter.py|アドオン1.11.0。日本語UI、起動方法別表示、定義選択、数値範囲、対象一覧、Validate／Export、新旧blendの識別。|
|tools/blender/create_stage01.py|付属旧形式ステージを再生成しても自動出現へ変わらないよう明示。|
|Tools/blender/create_stage03.py|ホテル生成時の旧形式を明示。|
|tools/blender/create_title.py|タイトル生成時の既存の専用出現を維持。|
|tests/EnemySpawnTests.cpp|3方式、地点別総数、遅延、同時／順次、上限待ち、再起動、Reset／状態コピー、旧Stage01～03、新旧Blender出力の回帰。|
|tests/EnemyPoolTests.h|実GameScene・D3D上で新方式の起動、再利用、残数、巻き戻し、リセット・再読込を検証。|
|tools/blender/test_spawn_activation.py|実Blenderで旧Stage01～03の再出力一致、新形式Validate／Export、保存再読込、元ファイルのハッシュ保持を検証。|
|tools/test-blender-spawn-activation.ps1|新BlenderテストとC++テストの実行入口。|
|CG2_Setup.vcxproj|テストを含むGameApp.cppがCOFFセクション数上限に達したため、そのファイルだけ/bigobjを追加。|
|docs/blender-level-editor.md|新設定の手順への案内と旧形式の説明の区別。|
|docs/enemy-spawn-activation.md|操作手順、仕様、JSON例、API、互換・移行方針。|
|docs/enemy-spawn-implementation-report.md|本報告。|

## 互換性と保護したデータ

- 旧JSONでactivationがない地点は自動起動させません。旧Triggerの共有総数・共有上限・抽選・順番・出現位置・時間を維持します。
- 複数地点へ総数をコピーすると敵数・抽選・共有上限が変わるため、自動分割をしません。旧形式を互換読込し、独立設定への切替は明示操作にしました。これが今回の移行方式です。
- 既存SpawnGroupの時刻・Sequential／Simultaneous・行別敵定義・出現位置は維持します。
- Stage01～03のblend／JSON／glTFを自動上書きしていません。開始時点にあったStage03のstage03.json、stage03.gltf、shutter_000.gltfの未コミット変更も保持しました。
- インストール済みBlenderアドオンは旧版をgenerated/blender-addon-backups/20261019-spawn-activationへ退避し、検証済み1.11.0へ更新しました。ソースとのSHA256一致を確認しました。起動中のBlenderの編集内容や設定ファイルは操作していません。

## 検証結果

|検証|結果|
|---|---|
|tools/test-blender-spawn-activation.ps1|成功。Stage Start、複数地点のTrigger起動、各地点の独立総数、両出現方式、遅延・間隔、上限待ち、Manual、リセット、検証・保存・再読込・Exportを確認。C++で出力JSONも実行。|
|tools/test-blender-level.ps1|成功。既存のレベル検証、変換、抽選・カタログ、保存、出力先ロック解除、置換失敗時の復元。|
|tools/test-blender-spawn-groups.ps1|成功。既存の時刻指定出現・UI編集・明示定義・順序・保存再読込の回帰。|
|tools/test-stage-loader.ps1|成功。ステージJSON、エラー時の保持、既存の敵・武器・ゴール、座標・判定との互換。|
|tools/test-enemy-pool.ps1|成功。実GameSceneでStage Startの事前準備、Trigger、Manual、死亡後の残数、プール再利用、レンダラー再初期化なし、状態復元・リセット・再読込。既存SpawnGroupの実シーン再入場も確認。|
|Debugビルド|成功。警告0・エラー0。build-Debug.log。|
|Releaseビルド|成功。警告0・エラー0。build-Release.log。|
|git diff --check|成功。|

Stage01～03はJSON読込と旧blendの別フォルダーへの再出力一致で互換性を確認しました。各ステージを手操作で全通路プレイする確認は行っていません。Blenderのヘッドレス終了時には既存テストでも出る未解放メモリの診断がありましたが、検証の終了コードは0です。

実D3Dテストの最初の実行では、追加したシーン生成回数が既存の単調増加SRV割り当て（上限512）を超え、SRV index overflowで停止しました。追加テストを既存シーン／プールの再利用へ変更し、再実行で最後まで成功しました。エンジン全体のSRV割り当て方式や上限は変更していません。

## 対象外・制限

指定された出現機能の実装・自動検証は完了しています。実行中の地点への起動要求は予約せずfalseを返します。旧共有設定を独立設定へ切り替えるときは、地点ごとの総数等を確認して指定してください。

敵AI・行動・アニメーション・性能・敵種類・ランダム総数・Box／Radius出現範囲・シャッター開放率イベント・イベント編集UIは今回追加していません。

BlenderをCtrl+Sで保存して再起動すると1.11.0の日本語UIが反映されます。既存地点で新機能を使う場合は「起動方法」を「旧形式（互換）」から必要な方式へ切り替え、敵定義と総数等を設定してください。

## 1.11.1：アドオン有効化時の修正

1.11.0ではregister内で旧データの判定を実行していたため、Blenderの「ディスクからインストール」や有効化で `_RestrictData object has no attribute objects` が発生しました。Blenderはこの間、bpy.dataへのアクセスを禁止します。従来のテストはregisterを直接呼んでいたため、この制限を再現できていませんでした。

1.11.1では判定を登録完了後のタイマーへ延期し、無効化時にタイマーも解除します。タイマー実行前にスクリプトからValidate／Exportした場合も、通常のデータアクセスに戻った段階で判定するため、既存地点が自動出現へ変わることを防ぎます。ファイル読み込み時の旧形式判定も維持しています。

tools/blender/test_level_addon_registration.pyを追加し、実際のaddon_install／addon_enable／addon_disableオペレーターを通して検証しました。制限下の登録成功、遅延初期化、有効化直後の検証、無効化・再有効化、旧ファイルの読込、新規配置の既定値、元blend不変を確認しています。tools/test-blender-spawn-activation.ps1にもこの検証を組み込みました。

## 1.12.0：種類別の確定体数

「敵の設定」に「1種類／種類ごとの体数」を追加しました。Normal 3体＋Fast 2体のように種類と体数を行ごとに指定し、合計を総出現数として自動表示・出力します。順次出現は行の順番で進み、同時生存上限は全種類の合計です。空き待ちや巻き戻し、リセットでも種類別の残数が維持されます。既存の単一種類の設定と旧形式はそのままです。

変更はEnemySpawnSystem.h/cpp、GameScene.cppの出現デバッグ表示、Blenderアドオン、関連テストと手順です。新JSON項目enemyCompositionは敵ID・体数の配列で、既存enemyと混在させず、spawnCountとの合計一致・敵定義・整数範囲・最大合計を検証します。既存の共通出現ループ・EnemyPool取得経路を使用します。

tools/blender/test_spawn_composition.pyを追加し、実Blenderで種類選択・行追加削除・並べ替え・合計・1種類との切替・Validate・保存再読込・Exportを確認しました。C++は3起動方法×2出現方式、確定体数と順序、上限待ち、リセット・状態復元、2引数コールバック互換、Blender出力JSONを検証します。D3Dテストにも同一地点の通常3体＋高速2体の確定出現とプール再利用を追加しました。

既存ステージの回帰テストは、作業途中のユーザーファイルを修正せずに実行できるよう、Git HEADの旧blendをgenerated内の検証用ファイルへ取り出して使用します。元のStage01～03全ファイルのハッシュも前後で確認します。編集中のホテルには起動対象のないトリガー「立方体」がありましたが、配置や設定は変更していません。

1.12.0の最終検証はtest-blender-spawn-activation.ps1（実インストール、旧形式、独立起動、種類別体数、C++連携）、test-blender-level.ps1、実D3Dのtest-enemy-pool.ps1がすべて成功し、Debug／Releaseとも警告0・エラー0でした。インストール済みアドオンも旧版を退避して1.12.0へ更新し、ソースとのハッシュ一致を確認しました。
