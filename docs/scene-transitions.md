# 共通の黒フェードとシーン遷移

`Engine/Core/FadeManager` が黒Spriteの透明度を管理し、`SceneManager` がシーンをまたいで保持します。3D・ポストエフェクト・HUDの後に描画するため、黒くなったときに照準やHUDだけが残ることはありません。DebugではScene画像全体、Releaseではゲーム画面全体を覆います。

## 別のシーンで使う

```cpp
// 登録済みシーン名、FadeOut秒数、FadeIn秒数。
// GameOver → Game（Retry）、ステージ間なども同じAPIを使用します。
if (app.Scenes().TransitionTo("Game", .75f, .75f)) {
    // 必要なら、遷移元の操作を止める状態に切り替えます。
}
```

遷移中の再要求、未登録シーン、負の時間・非有限の時間はfalseを返します。同じシーン名への遷移も可能なのでRetryに使えます。`IsTransitioning()` で遷移全体の完了、`Fade().IsFinished()` で現在のフェード完了、`Fade().Alpha()` で黒の透明度を確認できます。

流れは **FadeOut → 完全な黒フレームの描画 → 次のUpdateでOnExit/OnEnter → 遷移先の黒フレーム描画 → FadeIn → 通常更新** です。描画前に複数回Updateされても、切替前後の黒フレームを飛ばしません。ロード時間はFadeInの時間に加算しません。

FadeOut中は遷移元のUpdateを続けるため、爆散や破片が動き続けます。入力の停止は遷移元の状態に応じて設定します。FadeIn中は遷移先のUpdateを停止し、プレイヤー、敵、ステージ時間を進めません。OnEnter内でUpdateを呼ぶシーンは、GameSceneと同様に `IsTransitioning()` を確認して描画準備だけを実行してください。GameSceneはフェード完了後、開始射撃の長押しが離されるまで発砲を抑制します。

従来の `Change(app, name)` / `RequestChangeScene_` は即時切替を維持します。`Change` は進行中のフェードを取り消し、透明に戻します。今回、ほかの既存シーンの遷移演出は変更していません。

## FadeManager単体のAPI

- `Initialize(spriteCommon, dx)`：白1pxテクスチャを使った画面サイズの黒Spriteを準備。
- `FadeOut(seconds)`：現在の透明度から黒へ。
- `FadeIn(seconds)`：黒から透明へ。
- `Update(dt)` / `IsFinished()`：時間更新と完了判定。
- `Draw()`：描画先のポストエフェクト・UIの後に描画。
- `Reset()`：透明に戻す。時間0は即時完了、負・非有限の時間は例外、非正・非有限のdtは無視。

通常のシーン遷移には各シーンでFadeManagerを作らず、SceneManagerの `TransitionTo` を使ってください。

## タイトルからStage01

GAME STARTへの初回命中で既存Enemyの死亡・面破片の爆散を開始。TitleStartSequenceは0.45秒の待機だけを担当し、その後 `TransitionTo("Game", .75f, .75f)` を1回要求します。Stage01は最初のフレームから完全な黒で覆われます。

`tools/test-title-scene.ps1` は実D3Dで爆散待機、フェード中間値・完了、重複要求、黒フレーム前の切替防止、HUD込みの画面全ピクセルの黒、Stage01の更新停止・再開を検証します。`generated/title-tests/` に爆散、フェード中間、切替前後の黒、通常画面を保存します。
