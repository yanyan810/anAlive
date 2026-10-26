# 死亡専用ポストエフェクト

2026-10-26。既存のカメラ・死亡姿勢・演出時間・敵・徘徊・メニューは変更せず、背景の見え方だけを加えています。

虹色モザイクの描画方式を調査し、独立した色ノイズの重ね合わせと時間変化へ更新しました。現行設定は強度0.26、色0.8、サイズ0.006、模様変化速度0.35です。強制表示1.0もGPUへ届きます。[描画調査・強制表示・変更ファイルと検証記録](GAME_OVER_MOSAIC_RENDERING.md)を参照してください。

## モザイク強度・閉眼までの赤ビネット調整（変更履歴）

この段階ではモザイクの最大強度を0.055→0.12、色の強さを0.45→0.65へ変更しました。サイズ0.006、開始0.35秒、ピーク1.95秒、減衰3.5秒は維持しました。その後、視認性の不足を受けて上記の新方式へ更新しています。

赤が早く消えていた原因は、Collapse中の `blackAt` による赤→黒の補間と、FloorHold / Closingでの赤成分0でした。この補間と不要になった `blackAt` を削除し、Death / Collapse / Landing / FloorHold / Closingは設定した赤を保つようにしました。死亡直後の `staggerVignette` は0.16→0.28へ強め、既存の0.075秒の立ち上がりを使います。Collapse 0.36、Closing 0.50と赤色(0.10, 0.004, 0.010)は維持します。

3Dシーンへ赤いポストを適用し、その後に既存の暗転・黒いまぶたを描きます。Closing中に赤を独自にフェードさせず、見えるシーンの範囲だけに赤が残ります。完全閉眼のCorpseで専用ポストをバイパスし、Opening以降のビネット色は黒とするため、死亡時の赤は再出現しません。Blur / Desaturation / Chromatic Aberration、まぶた・カメラ・状態遷移・敵AI・メニューの処理は変更していません。

今回の変更ファイル：`Game/GameOverSequence.h` / `.cpp`、`tests/GameOverSequenceRuntimeTests.h`、`tests/GameOverPostEffectRuntimeTests.h`、本ドキュメント。レンダラーとシェーダー本体の変更は不要でした。

ClosingのGPU検証では、同じ3Dシーン・まぶた・他のポスト効果のまま、ビネット色だけを黒にした参照画像と比較します。赤による画素差が見える部分に残り、まぶたの不透明部分は黒であることを確認します。

| キャプチャ | Closing経過 | まぶたの閉眼率 | 赤ビネット強度 |
| --- | --- | --- | --- |
| `06a-closing-start.png` | 0.0007秒 | 約0.3% | 0.36 |
| `06b-closing-middle.png` | 0.35秒 | 87.5% | 約0.423 |
| `06c-closing-almost-closed.png` | 0.56秒 | 99.2% | 約0.483 |

完全閉眼は `07-darkness.png` の全画素が黒であることも検証します。開眼・Risingのログでは赤色0、モザイクは開眼後0.12へ上がり、Rising中に低下、Possess中に0となります。モザイク単体の新旧GPU画像を比べ、初期値の変更が実際の画素差を増加させる一方、元の絵を大きく崩さない範囲であることも確認します。画像・`trace.txt`・`post-gpu.csv` は `generated/gameover-tests/stage03/Debug/` と `Release/` に保存します。

調整後の結果：Debug / Releaseビルドとも警告0・エラー0、両構成の実GPU検証に成功。Closingの3時点すべてで赤が残り、完全閉眼は全黒、Opening / Risingで赤の再出現なし。4解像度、30 / 60 / 144 FPS相当の時間刻み、UI分離、Retry / Title / 途中終了時の復旧も成功しました。

## 開眼後の虹色モザイク

既存の死亡用PSで、細かな正方形の色ノイズを独立した色として重ねます。画像のUVや輪郭は崩さず、灰色でも虹色が見える合成にしています。タイルごとに色・濃さ・明るさを変え、約2.86秒かけて次の模様へ滑らかに変わります。暗部にも小さなノイズ輝度を与えますが、まぶたの黒やCorpseには適用しません。追加画像・テクスチャ参照・描画パスは不要です。

合成は既存のBlur / 色収差 / 彩度低下 / ビネットの後、まぶた・GAME OVER UI・シーンフェードの前です。UIの文字やまぶた自体は着色しません。通常ゲーム中、死亡から完全な暗闇までは強度0、Wanderでも0です。既存のフレーム単位およびシーン終了時のリセットをそのまま使います。

初期値での見え方は以下です。強度は最大ブレンド率で、タイルごとのムラにより実際はこの25〜100%です。

| 状態 | 虹色モザイク |
| --- | --- |
| Death〜Corpse | なし |
| Opening | 開眼開始0.35秒後から滑らかに増加。Opening終わりで最大の約98% |
| Awake | Opening開始1.95秒後（全開0.15秒後）に最大26%。Rising開始まで維持 |
| Rising | 開始から3.5秒のSmoothstepで減衰。中間で約14.7%、終了時に約0.54% |
| Possess | 開始0.3秒後に完全に消える |
| Wander | なし |

`Settings.post` の追加項目：

| 項目 | 初期値 | 単位・調整内容 |
| --- | --- | --- |
| mosaicEnabled | true | モザイク単体のON/OFF。既存enabledでも一括OFF |
| mosaicStrength | 0.26 | 最大強度。0〜1。1は強制表示確認用 |
| mosaicCellSize | 0.006 | タイル一辺／画面短辺。0.001〜0.05。720pで約4.3px、1080pで約6.5px。縦長画面でも正方形 |
| mosaicColorStrength | 0.8 | 虹色の色の強さ。0〜1。0はモザイク無効 |
| mosaicChangeRate | 0.35 | 1秒当たりの模様の更新数。0は固定模様 |
| mosaicStartSeconds | 0.35 | Opening開始から出始めるまでの秒数。Opening区間内に制限 |
| mosaicPeakSeconds | 1.95 | Opening開始から最大になるまでの秒数。開始時刻より後、Rising開始までに制限 |
| mosaicDecaySeconds | 3.5 | Rising開始から消えるまでの秒数。Rising＋Possessの長さまでに制限 |

出現・減衰は既存の区間時計から評価し、Opening→Awake→Rising→Possessの境界でも連続します。短い演出時間へ変更した場合や長い減衰時間を指定した場合も、Wander前に0になります。

初回のモザイク追加で変更したファイルは `Game/GameOverSequence.h` / `.cpp`、`Engine/Render/DeathPostEffect.h`、`Engine/Render/RenderManager.cpp`、`resources/shaders/DeathPostEffect.PS.hlsl`、`tests/GameOverSequenceRuntimeTests.h`、`tests/GameOverPostEffectRuntimeTests.h`、本ドキュメントです。

追加後の検証：Debug / Releaseビルドとも警告0・エラー0。両構成でStage03の演出検証、30 / 60 / 144 FPS相当の時間刻み比較、4解像度の実GPU出力、種類別・一括無効化、色の強さ0でCopyと全画素一致、UIの非着色、Retry / Title / 開眼途中のシーン終了でのリセットに成功しました。同じタイル位置の色が解像度を変えても一致すること、初期値の変化が控えめな範囲に収まることも検証しています。実際の開眼後画像と各段階の強度ログは `generated/gameover-tests/stage03/Debug/` / `Release/` に保存しています。

## 描画と再利用

既存RenderManagerのフルスクリーン三角形、ルートシグネチャ、中間バッファ、SRV/RTV、PSO生成処理を再利用しました。既存Grayscaleの輝度係数も使用します。
既存のVignetteは黒の強度固定、GaussianBlurは全画面に17タップ×2方向であり、色収差は未実装でした。そのため、色・強度を調整できるビネット、彩度ブレンド、中心を保護する軽い3×3 Gaussian、短い色収差を1つの死亡専用PSに統合しています。

通常の効果なし描画では、元のCopy用PSOを死亡用PSOへ置き換えます。描画回数・中間テクスチャ数は増えません。通常は1タップ、周辺のBlur時は最大9タップ、色収差も同時に有効な場合は最大11タップです。
オブジェクト／パーティクル合成後の背景にも対応します。DebugのSceneテクスチャで既存の別ポストエフェクトやレイヤー合成を併用する場合だけ、最後に1パスを追加します。通常プレイ／無効化時は以前の描画経路をそのまま通ります。

描画順は **ステージと既存レイヤーの合成 → 死亡専用ポスト → GAME OVER UI・まぶた → SceneManagerのFade** です。UIやまぶたへBlur・色収差を適用しません。
シェーダーはTexture.GetDimensionsからテクセル幅を求め、マスクは正規化UVで計算します。解像度・縦横比・端数サイズに依存する固定1280×720の値はありません。各タップは半テクセル内へ制限して端の回り込みを防ぎます。元の不透明なAlphaも維持します。

## 演出時計との同期

| 状態 | 効果 |
| --- | --- |
| Stagger（Death） | 暗赤色ビネットを短く立ち上げ、死亡直後に短い色収差 |
| Collapse | カメラの倒れ込み進行率に合わせて彩度・周辺の焦点を失い、赤ビネットを強める |
| Landing | カメラの接地と同じ時刻を基準に、色収差と追加Blurを短いsin²パルスで発生 |
| FloorHold / Closing | 赤いビネット・彩度低下・周辺Blurを維持しながら閉眼へ |
| Corpse | 既存のまぶたで完全な黒。見えない背景の死亡用PSはバイパス |
| Opening | 黒いビネット・低彩度・弱いBlurへ滑らかに戻す。死亡時の赤は戻さない |
| Awake | 弱い開眼後の値を保持 |
| Rising / Possess | 状態時計に従って段階的に減衰。Possessの終わりで0 |
| Wander / Inactive | 死亡専用処理なし。通常のCopyへ完全復帰 |

GameOverSequence::PostEffect()はカメラと同じ倍精度の区間時計・接地経過時間を使う純粋な評価関数です。モザイクの模様もOpening以降の区間時計から求め、独立した累積時計や毎フレームの乱数は持ちません。演出の既存時間も変更していません。
Corpseでは全黒のまぶたの下でバイパスへ切り替えるため、見える画面は連続します。Openingの始点も全閉眼です。その他の見える状態境界は強度を一致させています。

## Settings.post

調整箇所は `Game/GameOverSequence.h` の `Settings::post`（PostSettings）です。Blurと色収差はソース画像のピクセル単位、色はLinear RGB、時間は秒です。

| 項目 | 初期値 | 調整内容 |
| --- | --- | --- |
| enabled | true | 死亡専用ポスト全体のON/OFF |
| vignetteEnabled / desaturationEnabled / blurEnabled / chromaticEnabled | true | 種類ごとのON/OFF |
| red | (0.10, 0.004, 0.010) | 死亡直後の暗赤色 |
| staggerVignette / collapseVignette / closingVignette | 0.28 / 0.36 / 0.50 | 硬直・倒れ切った時・閉眼時の赤ビネット強度 |
| staggerVignetteFadeSeconds | 0.075 | 赤ビネットの立ち上がり時間。硬直時間までに制限 |
| collapseDesaturation / closingDesaturation | 0.50 / 0.72 | 彩度低下量。0で元の色、1で完全なグレースケール |
| collapseBlurPixels / closingBlurPixels | 0.65 / 0.90 | 周辺Blurの半径。中心の視界は保護 |
| openingVignette / openingDesaturation / openingBlurPixels | 0.16 / 0.24 / 0.35 | 全開から起立開始まで残す弱い効果 |
| staggerChromaticPixels / landingChromaticPixels | 0.65 / 0.75 | 死亡直後・接地時のRGBずれの最大幅 |
| landingBlurPixels | 0.40 | 接地パルス中に加える周辺Blur |
| staggerPulseDelay / staggerPulseSeconds | 0 / 0.14 | 死亡直後パルスの遅延・長さ。硬直中に収まるよう補正 |
| landingPulseDelay / landingPulseSeconds | 0 / 0.16 | 接地パルスの遅延・長さ。接地後の時計なので状態をまたげる |
| desaturationStart / blurStart | 0 / 0 | Collapse中の開始位置。倒れ込み進行率0〜1で指定 |
| recoveryStart | 0.10 | Rising中に効果の回復を始める時刻の割合 |
| riseRecovery | 0.80 | Rising終了までに回復する割合。残りをPossessで0へ |
| curve | 1.1 | 効果の補間カーブ（0.5〜4） |
| edgeInner / edgeOuter | 0.35 / 1.0 | 周辺マスクの開始・終了半径 |

RenderManagerでも有限値・上限を補正し、ビネット0〜0.85、彩度低下0〜1、Blur0〜3px、色収差0〜2pxに制限します。初期値はこれより控えめです。

## 復旧と無効化

- 描画フレーム開始に死亡用値を0へ戻し、GameSceneだけが現在のシーケンス値を設定します。通常プレイ・Title・他シーンは死亡用PSを使用しません。
- GameSceneのOnEnter / OnExit、SceneManagerの切り替え時にも明示的にリセットします。途中終了、Retry、Titleで残りません。
- enabled=false、全種類OFF、全強度0、Corpse、Wanderでは通常CopyのPSOを選びます。死亡用CB更新・死亡用シェーダー実行・追加の描画パスはありません。
- 専用PSOと256バイトCBは初期化時に1回だけ用意します。演出中のテクスチャ生成やリソース再確保はありません。
- OffscreenPassの既存の追跡状態を使い、SRV状態の出力を再びRTV扱いでバリアしないようにしています。読み書きに同じ中間バッファを使いません。

## 変更ファイル

- `Game/GameOverSequence.h`, `.cpp`: PostSettingsと状態時計による効果評価。
- `Game/scene/Main/GameScene.cpp`: 描画時の設定と出入り時のリセット。
- `Engine/Render/DeathPostEffect.h`: CPU/GPU共通の64バイト定数レイアウト。
- `resources/shaders/DeathPostEffect.PS.hlsl`: 既存4効果と虹色モザイクを統合したPS。
- `Engine/Render/RenderManager.h`, `.cpp`: 既存基盤への組み込み、通常Copyとの切替、描画回数の確認。
- `Engine/Render/OffscreenPass.h`: 追跡しているリソース状態の参照。
- `Engine/Core/SceneManager.cpp`: シーン変更でのリセット。
- `tests/GameOverSequenceRuntimeTests.h`: 各段階の処理後の画像、FPS比較、状態境界、復旧・無効化。
- `tests/GameOverPostEffectRuntimeTests.h`: 各効果のGPUピクセル検証、UI分離、バイパス同一性、実描画回数、GPUタイムスタンプ。
- `tools/test-gameover-runtime.ps1`: Debug / Release別に画像・演出ログ・GPU計測CSVを保存。
- `CG2_Setup.vcxproj`, `.filters`: 新しいヘッダー・シェーダー・テストの登録。
- `docs/GAME_OVER_SEQUENCE.md`, `docs/GAME_OVER_POST_EFFECTS.md`: 設定と検証記録。

## 検証データの場所

`tools/test-gameover-runtime.ps1` をDebug / Releaseで実行します。全状態と4解像度（1280×720、853×479、1920×1080、600×900）の実GPU出力を確認します。
`generated/gameover-tests/<stage>/<Debug|Release>/post-gpu.csv` に、Copyと5効果併用のGPU時間（μs）を記録します。128描画をまとめて計測し、Copyと効果ありを交互に、順序も入れ替えて比較します。最初の2組をウォームアップとして除外した8回の中央値です。VSyncやCPU待機を含めず、単独フルスクリーンPSのコストを測っています。ゲーム全体のフレーム時間ではありません。GPUクロックや他アプリの影響による変動があります。
`post-<幅>x<高さ>-0〜9.png` は、Copy / 彩度低下 / 黒ビネット / Blur / 色収差 / 5効果併用 / UIを後描き / モザイク単体 / モザイク色0 / 以前の小さい設定値、の比較画像です。10〜18は灰色背景・強制表示・時間変化・UIの検証です。詳細は描画調査資料を参照してください。無効時は全画素がCopyと一致し、UIの不透明な緑の領域は効果の後でも正確な緑を保つことを検証します。

## 2026-10-26の4効果実装時の検証結果（モザイク追加前）

- Debug / Releaseビルド成功、警告0・エラー0。
- Stage03の死亡からWanderまでの実描画、Retry / Titleのメニュー処理、途中のシーン終了、24通りのカメラ衝突、4姿勢×4方向の検証に成功。
- 30 / 60 / 144 FPS相当の時間刻み、および長いフレームで効果値と状態遷移を比較。見える境界での連続性と接地パルスの同期を確認。
- 4解像度のGPUピクセル検証、UI分離、無効時の全画素一致・専用描画0回を確認。既存Gaussian X/Yとの併用も通過。
- 既存の画面遷移GPUテストはDebug / Releaseとも成功。タイトルの描画・照明・破壊演出・シーン切替の回帰テストもDebugで成功。

同一環境・Releaseでの単独パス計測。効果ありは4効果併用（Blur 1px、色収差0.75px）。強度が小さい初期値を含めた実ゲーム全体のフレームレートを示すものではありません。

| 解像度 | 通常Copy | 4効果併用 | 差分 |
| --- | ---: | ---: | ---: |
| 1280×720 | 0.0067ms | 0.0699ms | +0.0632ms |
| 853×479 | 0.0033ms | 0.0220ms | +0.0186ms |
| 1920×1080 | 0.0213ms | 0.1148ms | +0.0935ms |
| 600×900 | 0.0047ms | 0.0372ms | +0.0325ms |

計測用のPNG・CSV・演出ログは `generated/gameover-tests/stage03/Debug/` と `Release/` に保存しています。ベンチマーク用の繰り返し描画・Query・Readbackはテスト実行時だけです。
