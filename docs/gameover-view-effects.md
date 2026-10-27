# 死亡時の二重視界・接地ブラー（試験実装）

## 操作

Debug / Release ともゲームのステージ内で **F9** を押すと、マウス操作を解放して
`View Post Effects (F9)` を開く。もう一度F9、またはウィンドウの×で閉じる。
通常の操作へ戻るときは Scene 画面をクリックする。

- Double Vision：Enable / Strength / Offset (pixels、X/Y) / Opacity / Sway Amount / Sway Speed
- Impact Blur：Enable / Strength (pixels) / Duration (seconds)
- `Preview Double Vision` だけをON：通常プレイ画面で二重視界だけを確認。
- `Preview Impact Blur` だけをON：接地ブラーだけを確認。
- 両方ON：同時に適用。
- `Hold Impact Peak`：ブラーのピークを固定して比較。初期値ON。
- `Replay Impact`：固定を解除し、接地ブラーの短い立ち上がり・減衰を再生。
- `Stop Preview`：プレビューを停止。
- `Reset Settings`：今回追加した2種類の設定を初期値へ戻し、プレビューを停止。

プレビューは専用の実時間時計を使う。HP、GameOverSequence のフェーズ・時計・カメラへは書き込まない。
プレビューは通常プレイ中だけに適用する。死亡演出中は設定値に基づく本来の時間制御で描画する。
Retry / Title / シーン終了時は、エフェクトとプレビューをリセットする。

## 初期値・設定

| `GameOverSequence::settings.post` | 初期値 | 調整範囲 |
| --- | ---: | --- |
| `doubleVision.enabled` | true | ON / OFF |
| `doubleVision.strength` | 1 | 0〜2 |
| `doubleVision.offsetPixels` | X=10、Y=3 | 各 -64〜64px |
| `doubleVision.opacity` | .32 | 0〜.8 |
| `doubleVision.swayPixels` | 12 | 0〜32px、二重像の揺れ幅 |
| `doubleVision.swaySpeed` | 1.4 | 0〜5Hz、揺れる速さ |
| `impactBlur.enabled` | true | ON / OFF |
| `impactBlur.strength` | 1.5 | 0〜3px |
| `impactBlur.duration` | .2 | .02〜2秒 |

二重視界の Strength はずれ幅と混合率へ掛ける。Offset の符号で重ねる方向を変えられる。
二重像はX/Yで異なる周期の滑らかな波を重ねて揺れる。Sway Amount / Speed のどちらかを0にすると固定像へ戻る。
揺れ幅も死亡時の強度に連動して現れ、閉眼までに弱まる。元の映像・カメラ・UIは揺らさない。
プレビューの揺れはImpact Blurの再生時計とは別の時計を使う。
Death 中は0→35%、Collapse中は35→100%に変化。接地後は弱まり、閉眼完了時に0になる。
Impact Blur は FloorHold 開始の `LandingTime()` に同期する。最大10msで素早く立ち上がり、
Duration の範囲で smoothstep により減衰する。カメラ揺れと同じ接地時計を使用する。

## 既存基盤の調査と実装

`RenderManager` は既存の FullScreen copy パスを、必要なフレームだけ死亡用の複合PSOへ切り替えていた。
そのシェーダーには3×3の周辺ブラー、色収差、彩度、ビネット、虹色ノイズが既にあった。
別パスや別テクスチャを増やさず、この基盤を拡張した。

- エンジンの `ViewPostEffects.h` に汎用設定・評価関数・パラメータを追加。
  呼び出し元が露出量と接地からの時間を渡す。ゲームオーバー状態への依存はない。
- 任意のシーンから `RenderManager::SetViewPostEffects()` で使用可能。
  `BeginOffscreen()` 後に呼び出す。既存の効果とは独立して二重視界と衝撃ブラーを指定できる。
- Impact Blur は既存3×3ブラーを関数化して再利用する。既存の周辺半径と衝撃半径の大きい方で
  一度だけブラーを実行し、ブラーを重複適用しない。
- Double Vision は同じ映像のずれた位置をRGBまとめて読み、透明度付きで混合する。
  ブラーも有効な場合は同じ関数でずれた像もぼかす。RGB別のオフセットは使わない。
- 定数構造体は96byte。既存の256byte定数バッファを使用し、フルスクリーン描画回数を増やさない。
- 2Dのまぶた・GAME OVER UIはポスト処理後に描画する。新しい効果は閉眼までで終了し、
  開眼後の虹色ノイズや敵消失時の完全遮蔽の設定・タイミングは維持する。
- Release は通常のゲーム表示を維持し、F9パネルを開いた間だけ既存のImGui Scene表示へ切り替える。

## 変更ファイル

- `Engine/Render/ViewPostEffects.h`（追加）：汎用設定・評価・適用
- `Engine/Render/DeathPostEffect.h`：既存複合PSOの追加定数・有効判定
- `Engine/Render/RenderManager.h` / `.cpp`：汎用API、プレビュー、ImGui調整
- `resources/shaders/DeathPostEffect.PS.hlsl`：映像の重ね合わせ、既存ブラー関数の共用
- `Engine/Core/GameApp.cpp`：プレビュー時計、F9、Releaseのパネル表示
- `Game/GameOverSequence.h` / `.cpp`：設定・死亡フェーズによる強度と時間の制御
- `Game/scene/Main/GameScene.cpp`：独立プレビューの適用、パネル表示、退出時リセット
- `tests/GameOverSequenceRuntimeTests.h`：実ステージの単独・同時プレビュー画像、時計独立性、復旧、時間制御
- `tests/GameOverPostEffectRuntimeTests.h`：RGB全体の二重像、中央ブラー、同時適用、無効時完全一致、UI非適用のGPU検証
- `CG2_Setup.vcxproj`：追加ヘッダーの登録

既存の未コミット変更を保持したまま追加している。

## 検証コマンドと画像

```powershell
./Tools/build.ps1 -Configuration Debug
./Tools/test-gameover-runtime.ps1 -Configuration Debug -Stage stage03
./Tools/build.ps1 -Configuration Release
./Tools/test-gameover-runtime.ps1 -Configuration Release -Stage stage03
```

`generated/gameover-tests/stage03/{Debug,Release}/`：

- `preview-00-original.png`：実ステージ、効果なし
- `preview-01-double-vision.png`：二重視界のみ
- `preview-01a-double-sway-020.png` / `preview-01b-double-sway-040.png`：0.2秒 / 0.4秒の揺れ比較
- `preview-02-impact-blur.png`：接地ブラーのみ、ピーク固定
- `preview-03-combined.png`：両方
- `preview-04-reset.png`：プレビュー終了、元の画面との完全一致を検証
- `01-stagger.png` / `03-falling.png` / `04-landing.png`：既存演出に統合した実画面

GPUテストは4描画サイズ、既存の虹色ノイズ・完全遮蔽・UI・無効時バイパスも併せて検証する。

### 検証結果（2026-10-27）

- Debug / Release の両ビルド成功。両構成の stage03 ランタイム・GPUテストが PASS。
- 実ステージで効果なし、二重視界、接地ブラー、同時適用の画像を確認。
  プレビューを終了すると元のゲーム画面と完全一致し、HP・死亡フェーズ・死亡時計は変化しない。
- 4描画サイズで、ずれた映像のRGB全体の混合が期待値に一致すること、中央部もぼけること、
  単独・同時適用、Opacity=0の完全コピー、2D UIの非適用を検証。
- 死亡時の段階的な増加、接地時計との同期、0.2秒の減衰、閉眼完了での終了、開眼後の非適用を検証。
- 既存の虹色ノイズ、敵消失時の完全遮蔽、まぶた、Retry / Title / 途中退出の回帰検証も PASS。
- 追加フルスクリーン描画は0。CPU/HLSL定数レイアウト96byteは静的検証済み。
- 二重像の揺れ追加後も Debug / Release のビルド・全ランタイムテストが PASS。
  0.2秒と0.4秒で実ステージのGPU画像とオフセットが変わること、揺れ幅/速度0で停止すること、
  死亡強度との連動、30 / 60 / 144 FPSでの時間一致、プレビュー終了時の完全復旧を確認。
