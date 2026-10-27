# GameOverSequence カラーノイズ

`GameOverSequence::settings.post` で調整する。既存の虹色パレット、短辺基準の正方形セル、強度の開始・ピーク・減衰カーブを使用する。

| Settings | 初期値 | 範囲・意味 |
| --- | ---: | --- |
| `mosaicChangeRate` | `20` | 0〜120 回/演出秒。0でパターン固定。従来の滑らかな補間速度から、パターンの切替頻度に変更 |
| `mosaicCellSize` | `.006` | .001〜.05、描画先短辺に対する粒の幅。720pで約4.32px |
| `mosaicStrength` | `.26` | 0〜1、既存の強度エンベロープに掛ける |
| `mosaicColorStrength` | `.8` | 0〜1、虹色の色の強さ。0でノイズなし |
| `mosaicDensity`（追加） | `1` | 0〜1、ノイズを表示するセルの割合。0でノイズなし |
| `mosaicRiseEndRateScale`（追加） | `.15` | 0〜1、Rising終了時の速度倍率。初期値では20→3回/秒 |

既存の `mosaicEnabled`、`mosaicStartSeconds=.35`、`mosaicPeakSeconds=1.95`、`mosaicDecaySeconds=3.5` は維持。

## 更新と描画

- Opening/Awakeは基準速度でランダム変化する。強度は既存設定に従って徐々に現れる。
- Risingは速度倍率をsmoothstepで1から終端倍率へ下げる。Possessは残った速度倍率を0へ下げる。強度の減衰は従来の設定に従い、Wanderでは完全に無効。
- CPUは既存のフェーズ時計から速度カーブの積分を解析的に求め、`mosaicTime`として渡す。減速中に時間ステップが逆行せず、速度積分用のフレーム累積を追加しない。
- GPUは `floor(mosaicTime * mosaicChangeRate + .0001)` を時間ステップとする。微小な許容値はFPSごとの浮動小数点丸めによる境界の差を吸収する。
- 画面セルのX/Yと時間ステップを整数ハッシュで混合し、色相・濃淡・密度の乱数を生成する。同じステップ内は同じパターン、次のステップは独立したパターンになる。スクロールやステップ間の補間は行わない。
- 従来の輝度正規化と限定した濃淡幅を維持する。混合率は最大.85に制限し、強度を1に設定しても背景を残す。初期強度.26には制限の影響なし。
- 既存のDeathPostEffectパスを再利用。CPU/HLSLの64byte構造体のpaddingを密度に使用し、割当済み256byte定数バッファ、描画回数、テクスチャ参照数は増やさない。追加テクスチャやCPU乱数の転送は不要。
- 通常プレイ、死亡〜閉眼、Corpse、Wanderはノイズなし。シーン処理後のGAME OVER UI・まぶた・フェードの描画順、他のポストエフェクト、AI、カメラ、死亡アニメーションは維持。

## 検証の再実行

リポジトリ直下で以下を実行する。

```powershell
./Tools/build.ps1 -Configuration Debug
./Tools/test-gameover-runtime.ps1 -Configuration Debug -Stage stage03
./Tools/build.ps1 -Configuration Release
./Tools/test-gameover-runtime.ps1 -Configuration Release -Stage stage03
```

`generated/gameover-tests/stage03/{Debug,Release}/` にGPU読み戻しPNGとCSVを保存する。

- `post-{解像度}-12/14/17.png`：同じ時間ステップで完全一致。`18.png`は次のステップ。
- `post-{解像度}-21〜32.png`：0.0125〜0.5625秒の12パターンをGPU描画。
- `post-1280x720-33〜41.png`：Opening/Awake/Risingそれぞれを30/60/144 FPSで更新して得た時計による描画。同じ経過時間の3枚が完全一致することを確認。
- `noise-temporal-gpu.csv`：各パターンの平均線形輝度と画素差。12パターンの輝度レンジが.003未満か検証。
- `mosaic-gpu.csv`：色の視認性、同一ステップ内の差、ステップ境界の差。
- `post-gpu.csv`：GPU timestamp計測。copy、既存効果のみ、ノイズを含む全効果、ノイズ分の差分（μs）。追加フルスクリーン描画は0。

4描画サイズ（1280×720、853×479、1920×1080、600×900）、密度0/.35/1、速度0、背景ディテールとアルファ保持、UIの非適用、実ステージのOpening/Awake/Rising/Possess/Wander、既存のフェーズ・状態リセットも検証する。

## 実測結果（2026-10-27）

- Debug / Releaseビルド成功。修正後の両構成で `test-gameover-runtime.ps1 -Stage stage03` がPASS。
- 各構成で185枚のGPU描画PNGを保存（本番ポストPSOの141枚、実ステージ・まぶたの44枚）。
- 同じ時間ステップ内は完全一致。Opening/Awake/Risingの各3枚も30/60/144 FPS間で完全一致。ステップ切り替え時の平均RGB画素差は約5.25/255。
- 12パターンの平均線形輝度レンジは最大.000064（0〜1基準）。初期設定の平均色差は約8.75/255、色差8以上の画素は約53%。
- UI・まぶた・背景ディテール・非表示フェーズ・既存エフェクト・状態リセットの検証もPASS。

GPU timestampによる1描画あたりの実測値（μs、使用GPUと動作クロックに依存）：

| 解像度 | Debug 既存効果のみ | Debug ノイズ込み | Release 既存効果のみ | Release ノイズ込み | Release 差分 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1280×720 | 62.788 | 74.184 | 80.032 | 79.140 | -.892 |
| 853×479 | 56.316 | 56.636 | 29.624 | 30.276 | .652 |
| 1920×1080 | 158.052 | 158.616 | 149.656 | 156.268 | 6.612 |
| 600×900 | 47.060 | 44.824 | 33.872 | 34.696 | .824 |

差分が小さい箇所ではGPUクロック等の計測ばらつきが含まれる。負の差分は高速化を意味しない。Releaseの1080pでは全パス約.156ms、ノイズ追加分約.0066ms。描画回数は既存の1回のままで、追加テクスチャ参照やCPU乱数転送は0。

検証途中で追加した計測テストの中央値参照に添字ミスがあり、Debug assertionが発生した。測定数に基づく中央値参照に修正後、両構成のビルド・全GPUテストを再実行してPASSを確認した。

## 変更ファイル

| ファイル | 変更 |
| --- | --- |
| `Game/GameOverSequence.h` | Settingsの速度初期値と密度・終端速度倍率 |
| `Game/GameOverSequence.cpp` | 既存の強度カーブを維持し、減速カーブの解析積分をGPUへ渡す |
| `Engine/Render/DeathPostEffect.h` | 64byte定数構造体のpaddingを密度に置換。密度0のバイパス |
| `Engine/Render/RenderManager.cpp` | 速度・密度の範囲制限 |
| `Engine/Render/RenderManager.h` | GPUテストへFPS別の時計を渡すためのfriend宣言 |
| `resources/shaders/DeathPostEffect.PS.hlsl` | セル座標・時間ステップからの整数乱数、密度、最大混合率 |
| `tests/GameOverSequenceRuntimeTests.h` | FPS別の時間ステップ一致、減速・Settings・状態制御の検証 |
| `tests/GameOverPostEffectRuntimeTests.h` | 複数フレームGPUキャプチャ・輝度安定性・密度・FPS画素一致・描画負荷計測 |
| `docs/gameover-color-noise.md` | 設定・実装・再実行手順・検証結果 |
