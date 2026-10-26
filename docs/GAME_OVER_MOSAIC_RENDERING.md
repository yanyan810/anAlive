# 虹色モザイクの描画調査と修正

## 原因と描画経路

GameScene::DrawRender → GameOverSequence::PostEffect → RenderManager::SetDeathPostEffect → b6定数 → DeathPostEffect.PSの経路です。モザイクは既存のBlur / Desaturation / Vignette / 色収差の後に合成し、最後にまぶた・GAME OVER UI・シーンフェードを描きます。他のポスト効果による上書きではありませんでした。

修正前の実D3D12描画で、通常設定0.12はSequenceからRenderManagerへ渡っていました。一方、SequenceとRendererの両方に0.2の上限があり、強制設定1.0は全開時でも0.2に切り詰められました。

旧シェーダーは元のRGBをタイル色で乗算し、元の輝度へ再正規化してから、強度×0.3〜1の係数で混ぜていました。独立したノイズの色を重ねる方式ではなく、細かなタイル・弱い色変化・低彩度との組み合わせで視認性が不足していました。時間パラメーターもなく、模様は静止していました。以前の小さな画素差の検証だけでは、十分に見えることを保証できませんでした。

## 強制表示と比較画像

指定の `mosaicStrength=1.0 / mosaicColorStrength=1.0 / mosaicCellSize=0.015` をGameOverSequenceへ設定し、Opening後半・Awake・Risingの3Dシーンを実レンダーターゲットからDirectXTexでReadbackしてPNGへ保存します。PIXやRenderDocのフレームファイルではなく、D3D12の実描画結果のキャプチャです。

- 修正前：`generated/gameover-tests/mosaic-before/`。Awakeでは要求1.0→Sequence 0.2→Renderer 0.2。色1.0、サイズ0.015は到達。
- 修正後：`generated/gameover-tests/stage03/Debug/` と `Release/`。
- `mosaic-force-trace.csv`：要求値、Sequence値、Renderer値、色の強さ、サイズ。
- `09-opening-late-mosaic-force.png`、`10-eyes-open-still-mosaic-force.png`、`13-rising-mosaic-force.png`：強制設定。
- 同名の `-mosaic-off.png`：同じ時計・カメラ・他の効果のまま、モザイクだけOFF。
- `09-opening-late.png`、`10-eyes-open-still.png`、`13-rising.png`：通常設定。

上限を両方とも0〜1へ改め、Awakeの強制表示では要求・Sequence・Renderer・GPUのMapped CBの強度がすべて1.0になることを確認するテストを追加しました。Opening / Risingは既存の出現・減衰カーブを保つため、強制設定でも状態に応じた係数が掛かります。

## 新しい合成

- 画面短辺を基準にした正方形セルごとに、独立した虹色と濃さを生成。元のUVや輪郭は変更しません。
- 元のRGBへ乗算せず、独立した色ノイズをアルファ合成します。灰色でも色が出ます。暗い背景でも見えるようノイズ輝度には小さな下限を設けています。
- ノイズの明るさは背景輝度に追従しつつ範囲を制限。通常設定では元の景色を最低74%残します。
- セル間の濃さ・明るさを変え、格子の輪郭線や均一な塗りを描きません。
- 開眼開始からの既存の演出時計で、隣り合う模様をSmoothstepでゆっくり補間します。色相は短い方向でつなぎ、更新の境界でも連続します。フレームごとに乱数を引き直しません。
- CPU/GPU共通定数は64バイトのまま、末尾の未使用領域へ時間と変化速度を追加。テクスチャや描画パスは増えません。

## 最終Settings.post

| 設定 | 初期値 | 内容 |
| --- | --- | --- |
| mosaicEnabled | true | 単体ON/OFF。既存enabledで全体OFFも可能 |
| mosaicStrength | 0.26 | 最大ブレンド強度。0〜1。通常は0.20〜0.30付近、1は強制確認用 |
| mosaicColorStrength | 0.8 | 色の強さ。0〜1。0は描画を省略 |
| mosaicCellSize | 0.006 | セル一辺／画面短辺。720pで約4.3px、1080pで約6.5px |
| mosaicChangeRate | 0.35 | 1秒当たりの模様の更新数。約2.86秒かけて次の模様へ。0は固定模様 |
| mosaicStartSeconds | 0.35 | Opening開始から出現まで。変更なし |
| mosaicPeakSeconds | 1.95 | Opening開始から最大まで。変更なし |
| mosaicDecaySeconds | 3.5 | Rising開始から消失まで。変更なし |

通常プレイ、Death〜Corpse、Wanderはモザイクなし。Openingで出始め、Awakeで最大、Risingで減衰、Possess開始0.3秒後に0です。状態遷移・まぶた・起き上がり・赤ビネット・他のポスト効果の時間と設定は変更していません。Retry / Title / シーン終了で時間を含めてリセットします。

## 検証項目とデータ

既存の `tools/test-gameover-runtime.ps1 -Configuration Debug|Release -Stage stage03` を使用します。

- 実3Dシーンで通常／強制／OFFを比較し、SequenceとRendererの値が一致することを確認。
- 1280×720、853×479、1920×1080、600×900の灰色画像へ実PSOを適用。強制表示と通常表示のRGBの色差、色のある画素の割合、時間変化の量を `mosaic-gpu.csv` へ記録。
- `post-<幅>x<高さ>-10.png`：灰色の基準画像。`-11.png`：強制表示。`-12.png`：通常。`-13.png`：1秒後。`-14.png`：0.01秒後。`-15.png`：変化速度0。`-16.png`：強制表示後にUI。`-17.png` / `-18.png`：模様更新境界の前後。
- 強制表示でもUIの不透明な緑は正確に緑のまま。まぶたの不透明部分は黒、Corpseは全画素黒。
- 30 / 60 / 144 FPSの時間刻みで同じ強度・模様の時計を比較。元のシーンのストライプが保たれ、無効化時は通常Copyと全画素が一致することを確認。
- Retry / Title / 演出途中のシーン終了、既存の赤ビネットのClosing開始・中間・99.2%閉眼、カメラ衝突とWanderの回帰検証。

## 変更ファイル

- `Game/GameOverSequence.h` / `.cpp`：初期値、変化速度、上限、既存時計の受け渡し。
- `Engine/Render/DeathPostEffect.h`：時間・変化速度の定数レイアウト。
- `Engine/Render/RenderManager.cpp`：上限1.0と有限値の補正。
- `resources/shaders/DeathPostEffect.PS.hlsl`：独立したブロック色の合成、濃さのムラ、時間補間。
- `tests/GameOverSequenceRuntimeTests.h`：実シーンの強制／OFFキャプチャ、受け渡しとFPS比較。
- `tests/GameOverPostEffectRuntimeTests.h`：灰色上の実GPU検証、UI分離、時間変化と明瞭さの計測。
- `docs/GAME_OVER_POST_EFFECTS.md`、本資料：設定と検証記録。

## 検証結果（2026-10-26）

Debug / Releaseともビルド成功、警告0・エラー0。両構成で全GPUテストとシーケンスの回帰検証が成功しました。通常・強制・OFFの実ステージ画像を目視し、灰色でも細かい虹色ブロックが見えることと、通常設定で景色が残ることを確認しました。強制設定はテスト内で一時的に使い、キャプチャ後に通常設定へ戻します。

1280×720の実測例（両構成で同値）：

| 項目 | 結果 |
| --- | --- |
| Awakeの強制表示 | 要求1.0 → Sequence 1.0 → Renderer 1.0 → GPU CB 1.0 |
| 通常設定・灰色上の平均RGB最大値−最小値 | 約8.79 / 255 |
| 灰色上でRGBの広がりが8以上ある画素 | 約53.4% |
| 強制表示・灰色上の平均RGB最大値−最小値 | 約50.52 / 255 |
| 通常設定、1秒後の平均RGB変化 | 約1.80 / 255 |
| 0.01秒後の平均RGB変化 | 約0.00031 / 255 |
| 模様更新境界の前後0.002秒の平均RGB変化 | 約0.00139 / 255 |

4解像度で色の出方は同程度です。無効時・色の強さ0は通常Copyと全画素一致、変化速度0は同じ固定模様、強制表示でもUIは非着色。Closingの赤、完全閉眼の黒、Wander前の消失、30 / 60 / 144 FPSの時計・状態遷移、Retry / Title / 途中終了でのリセットも成功しています。
