# 共通画面遷移 Phase 1

## 設計と既存構成

既存の `Engine/Core/FadeManager` を ScreenTransition 相当の共通機能として拡張しました。別のフェード描画クラスやシェーダーは追加していません。SceneManagerが所有するため、シーン破棄・初期化・ロード中もフェードの状態とSpriteが存続します。

- FadeManager：実時間、Alpha、色、補間、状態通知、全画面Sprite描画。
- SceneManager：遷移先、重複防止、ロード待機、シーン寿命、失敗時復帰。
- GameApp：実時間の更新と描画順序。
- TitleScene：GAME STARTの破壊と0.45秒の演出待機。

状態は `Idle → FadeOut → Hold → FadeIn → Idle`。FadeOut完了時はAlpha=1のHoldとなり、自動では解除されません。FadeInまたはResetで解除します。`StateStartCount(state)` / `StateEndCount(state)` は累積通知です。前回の値と比較すれば、時間0で開始・終了した状態も取得でき、シーンの寿命に依存するコールバックも不要です。Resetは設定と通知カウンターを保持します。

補間はLinear、二次EaseIn、二次EaseOut、二次EaseInOut。Progressは補間前の経過時間/期間です。時間0は即時完了、負・非有限の時間は単体APIで例外、シーン要求APIでfalseを返します。非正・非有限のdeltaは無視します。色は有限RGBを0〜1に制限し、Alphaは状態が管理します。

## 描画と時間

Release：3DとゲームUIをOffscreenへ描画 → ポストエフェクトをBackBufferへ描画 → Overlay UI → Fade。

Debug/Development：同じゲーム画像とOverlay UI・FadeをScene用テクスチャへ描画 → ImGuiでScene画像とデバッグ画面を描画。ImGuiはフェード中も操作できます。シーン切替中はシーン固有の編集操作を無効にし、共通パネルと状態表示を維持します。遷移中はマウスキャプチャとImGuiのNoMouseを解除します。

Spriteの透明度合成は `srcAlpha + dstAlpha * (1 - srcAlpha)` とします。ゲーム画像のAlpha=1を保持したままRGBへフェードを合成し、ImGuiのImageでフェードAlphaが再び適用されることを防ぎます。従来のDestBlendAlpha=ZEROでは、フェードイン中にSceneテクスチャのAlphaまで減り、最終表示が明るくなった後に再び暗くなる症状が発生していました。

白1pxテクスチャのSpriteを0〜1の正規化座標で描画するので、現在のビューポート全体を覆います。固定の1280×720をフェードのサイズとして保持しません。既存エンジンはゲーム描画解像度を1280×720に固定し、ウィンドウやScene画像の表示サイズへ拡縮します。この既存仕様でもフェードは端まで追従します。エンジン全体の動的なRenderTarget再生成・解像度設定機能は今回の対象外です。

GameAppのsteady_clockによる実経過時間を `UpdateTransition` にホストフレームごと1回渡します。シーンのdt制限、将来のスローモーション、DebugAIの固定dt・複数更新・更新停止から独立しています。初回GPU描画やウィンドウ停止で長い待ちが入ってもフェードの先頭を飛ばさないよう、ホストから渡す1フレームの進行を最大0.05秒に制限します。低FPSや長い停止時は、指定期間より実際の演出が長くなることがあります。SceneManagerの `Update(app, dt)` は1フレーム1更新用の互換APIで、テストなどではdtがそのまま実時間になります。

## シーン遷移API

```cpp
// 黒・Linear。登録済みの同じシーン名への遷移もRetryに使用可能。
bool accepted = app.Scenes().TransitionTo("Game", .75f, .75f);

// ホワイトアウトなど。色・補間はゲーム側が指定。
bool acceptedWhite = app.Scenes().TransitionTo(
    "NextStage", .5f, .5f, {1, 1, 1}, FadeEasing::EaseInOut);

bool active = app.Scenes().IsTransitioning();
bool waiting = app.Scenes().IsWaitingForScene();
const auto& error = app.Scenes().LastTransitionError();
const auto& fade = app.Scenes().Fade();
auto state = fade.State();
float alpha = fade.Alpha();
float elapsed = fade.Elapsed();
float progress = fade.Progress();
```

登録済みシーンだけを受理します。遷移中・手動フェード中・OnEnter/OnExit中の要求、負/非有限の時間、非有限の色、不正な補間値はfalseになります。シーン名を保存し、旧シーンのポインターやラムダを遷移要求へ保存しません。

1. FadeOut。旧シーンの更新を続け、爆散などを進める。
2. Hold。完全に覆ったフレームを描画するまで切り替えない。
3. 次のホスト更新でFactory → 旧OnExit → 新OnEnterを一度だけ実行。
4. 新シーンの準備完了までHold。未完成シーンの通常更新・描画を行わない。
5. 初期化済みの新シーンを完全に覆ったフレームとして描画。
6. 次の更新でFadeInを開始。そのdeltaは読み込み時間を含むため破棄。
7. FadeIn完了の次のホスト更新から通常のシミュレーションを再開。

時間0でも切替前後の不透明フレームを省略しません。シーンを破棄する前にGPU完了を待ち、退役シーンを回収します。Factoryの例外/nullでは旧シーンをそのまま保持。OnEnter・ロード通知の失敗では失敗シーンを終了し、旧シーンをOnEnterで再初期化してからフェードインします。この復帰では旧シーンの進行状態を完全保存しません。復帰自体も例外になった場合はシーン参照を解放してエラー表示を維持し、登録済みシーンへの再要求を受け付けます。

既存の `Change(app, name)` / `RequestChangeScene_` は即時切替を維持します。Changeは進行中のフェードを取り消します。描画中には即時Changeを呼ばず、RequestChangeScene_またはTransitionToで更新時に処理してください。

## ロード待機API

同期シーンはOnEnter完了が読み込み完了で、ISceneの追加APIを実装する必要はありません。非同期化する場合は次の3つを実装します。

```cpp
void UpdateLoading(GameApp& app, float unscaledDt) override;
bool IsReadyForTransition() const override;
std::string InitializationError() const override;
```

OnEnterで読み込みを開始し、UpdateLoadingで必要なメインスレッド処理を行います。準備中はIsReadyForTransition=false、完了時はtrue、失敗時はInitializationErrorにメッセージを返します。SceneManagerは完了までHoldを維持します。GameSceneはStage01の読み込み失敗を通知し、TitleSceneは既存のready/errorを通知します。実際のバックグラウンドローダーは未実装です。

## 単体の共通フェード

```cpp
auto& fade = app.Scenes().Fade();
fade.SetDuration(.5f);
fade.SetColor({0, 0, 0});
fade.SetEasing(FadeEasing::EaseOut);
fade.FadeOut();          // 設定された期間。FadeOut(.5f)も可能。
// 完了後はHold。シーンを替えない演出にも利用できる。
fade.FadeIn();           // 不透明状態から透明へ。
fade.Reset();
```

シーン遷移にはTransitionToを使い、その進行中はFadeを直接リセット・変更しないでください。単体フェードのSetDuration/SetEasingは進行中にも反映されます。期間変更は現在の経過秒数を維持して再計算します。FadeInは既存APIと同じくAlpha=1から始まります。

## タイトルとImGui

GAME STARTへの初回命中 → 既存の破壊・破片・カメラ演出 → TitleStartSequenceの0.45秒待機 → 黒Linearの0.75秒FadeOut → Stage01初期化 → 0.75秒FadeIn。既存のBeginとtransitionRequestedで連続命中を防ぎ、SceneManagerでも要求を二重に受理しません。GameSceneの開始射撃抑制も維持します。

Debug/Developmentの共通 **Screen Transition** ウィンドウを使用します。タイトル、Showroomなどどのシーンでも利用可能です。Escでマウスを解放してください。

- Fade Out / Fade In / Fade Out -> Fade In / Reset。
- Fade Duration、Fade Color、Fade Easing（単体フェード中も変更可能）。
- TransitionState、Alpha、Elapsed、Progress、Scene switch waiting、Scene transition、Destination、Last error。

シーン遷移中は設定・テスト開始を無効にし、遷移の状態を壊さないようにしています。Fade Out -> Fade Inは不透明な描画を1回経て戻ります。

## 変更ファイル

- Engine/2D/SpriteCommon.cpp：不透明なScene画像のAlphaを保持する合成。
- Engine/Core/FadeManager.h/.cpp：共通フェード拡張。
- Engine/Core/SceneManager.h/.cpp：待機、失敗復帰、デバッグ。
- Engine/Core/IScene.h：読み込み状態通知。
- Engine/Core/GameApp.h/.cpp：実時間の更新、テスト起動。
- Game/scene/Flow/TitleScene.h/.cpp：既存タイトル遷移への接続、初期化通知。
- Game/scene/Main/GameScene.h：初期化通知。
- tests/ScreenTransitionTests.h、Tools/test-screen-transition.ps1：機能/GPU回帰テスト。
- CG2_Setup.vcxproj：テストヘッダー登録。
- docs/scene-transitions.md：設計、利用方法、制約。

## 検証方法

Debug/Releaseをビルド後、`Tools/test-screen-transition.ps1 -Configuration Debug` および `-Configuration Release` を実行します。4補間、端点、Hold、時間0と状態通知、実時間とシミュレーションの分離、設定変更、不正入力、重複・再入防止、遅延ロード、Factory/null/OnEnter/遅延ロード失敗の復帰を検証します。実D3D12で853×481、1920×1080、127×255の全ピクセルを読み戻し、下のUIと画面端が完全に覆われることを確認します。Debugでは共通ImGuiウィンドウの存続とマウス入力有効も検証します。結果とPNGはgenerated/transition-testsへ出力します。

既存の `Tools/test-title-scene.ps1` で破壊演出の回帰、0.45秒待機、タイトル→Stage01、HUD込みの切替前後全黒画像、ロード時間除外、FadeIn中の更新停止と再開を確認します。

### 実施結果（2026-10-13）

- Visual Studio 2022のx64開発環境でDebug/ReleaseのソリューションRebuildに成功（ゲームとDebugAIViewer）。
- 共通フェードのDebug/Release実GPUテスト：両方PASS。generated/transition-tests/result-Debug.txt、result-Release.txtに保存。
- 既存タイトル実GPUテスト：PASS。generated/title-tests/result.txtに保存。既存破壊演出・照明・弾判定の回帰も通過。
- 切替前後の全黒画像、半分フェードしたゲーム画面と通常画面を確認。ゲームUIもフェードに含まれます。
- ImGuiはパネルの描画とマウス入力有効を自動確認。各ボタンの手操作によるクリック検証は行っていません。
- git diff --check：成功。開始時のリポジトリに未コミット変更はありませんでした。

追加修正：クリア結果のRETURN TO TITLE（クリック/Enter）から共通フェードでタイトルへ戻る処理を追加しました。Debug/Releaseのビルドと実GPUテストで結果固定・重複防止・復帰を確認し、5秒の初回描画待ちを想定したテストでもFadeInが先頭から進むことを確認しました。既存タイトル回帰テストも再実行してPASSです。

最終表示の再暗転を修正する前に、実ImGui合成の連続フレームで症状を再現しました。`generated/transition-tests/fade-in-frames-before-fix.csv` ではFadeInのAlphaが0.5から0.1へ減る間に最終表示が再び暗くなることを確認できます。新しいテストはRGBだけでなくSceneのAlphaと最終表示の明るさを検査し、ステージ開始とクリア→タイトル復帰の完了後にも複数のホストフレームを進めてフェードが再開始しないことを確認します。

合成修正後のDebug/ReleaseビルドとGPUテスト、既存タイトル回帰テストはPASSです。Debugの最終表示は15フレームを通して明るさが減少せず、SceneのAlphaも255を保持しました。ステージ開始とクリア→タイトル復帰では、それぞれ完了後12ホストフレームのIdle継続とフェード開始カウンターの不変を確認しました。修正後のImGui表示の計測は `generated/transition-tests/fade-in-frames-Debug.csv` に保存しています。

最初に既存build.ps1が選んだ最新Visual Studio環境でPDBバージョン不一致が発生しました。Visual Studio 2022のvcvars64.batを読み込み、MSBuildで生成物を再構築することで解消しました。検証にはこの2022開発環境を使用しています。

## 今回の範囲

GAME OVER、敵化視点、自動徘徊、ステージクリアの本格実装、状態引継ぎ、Iris、スローモーション、非同期ローダーは追加していません。今後はゲーム側で遷移先や引継ぎデータを決めてTransitionToを呼べます。新しい画面効果はFadeManagerのプレゼンテーション部分を拡張し、SceneManagerのロード・寿命管理を共用できます。既存のCircleMaskやRenderManagerのDissolveとは統合していません。
