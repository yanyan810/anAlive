# GameOverSequence 敵の認識消失

開眼完了後の Awake で、通常のプール管理敵を一斉に描画しなくする。
対象は `GameScene::enemies_` 全体（Normal / Ranged / Fast / Tank / Bomber）で、
演出専用の敵化プレイヤーは対象外。EnemyType::Normal だけに限定しない。

## 初期タイミング

- 死亡〜暗転〜開眼完了までは敵の描画を維持する。
- 開眼完了 +0.30秒：既存の虹色ランダムノイズの増幅を開始する。
- +0.375秒：増幅のピーク。通常の強度へ +0.22 を加算する。
- +0.375〜0.525秒：消失専用の虹色ノイズで画面を覆う。端だけ短く滑らかに切り替える。
- +0.40〜0.50秒：画面全体が完全な虹色ノイズになり、背景や敵の輪郭は見えなくなる。
- +0.45秒：全画面ノイズの中央で全敵の描画を1フレームで止める。
- +0.60秒：Awake 終了、既存の Rising → Possess → Wander を続ける。
- Wander / GAME OVER 中も非表示を維持する。フェード、消失音、消失パーティクルは追加しない。

増幅は sin² の滑らかな往復で、既存の輝度を抑制する虹色ノイズをそのまま使う。
画面の白黒反転や追加のフルスクリーン描画は行わない。
増幅終了後は元の強度エンベロープ・Rising / Possess の減衰へ戻る。
消失専用ノイズは背景から独立した明るさを使い、完全遮蔽中は全ピクセルをノイズで置換する。
通常の開眼ノイズにある85%混合率やセルごとの透けを、この短い区間だけ上書きする。

## Settings

| 場所 / 設定 | 初期値 | 意味 |
| --- | ---: | --- |
| `settings.enemyDisappearDelaySeconds` | `.30` | 開眼完了から増幅開始までの秒数。0〜10 |
| `settings.enemyDisappearNoiseSeconds` | `.15` | 増幅の継続秒数。終了時に消失。0.01〜2 |
| `settings.post.enemyDisappearNoiseBoost` | `.22` | 虹色ノイズへの加算強度。0〜1、合計も0〜1 |
| `settings.post.enemyDisappearNoiseOcclusion` | `1` | 消失専用ノイズの遮蔽率。1で背景を完全に隠す。0で従来の増幅のみ |
| `settings.awakeSeconds` | `.60` | 開眼後の静止時間。必要に応じて下記の最小時間まで延長 |

Awake は最低でも「開始待ち + 増幅時間 + 消失後0.15秒」の長さを確保する。
長いノイズ時間を設定した場合は、遮蔽が消えるまで Awake を延長する。
NaN / infinity のタイミング設定は初期値へ戻す。強度の非有限値は0として扱う。
ノイズ無効・強度0でも消失タイミングは進む。視界内の敵の有無、敵数に依存しない。

## 描画・状態・復旧

`GameOverSequence::EnemiesVisible()` を描画判定に使用する。判定は Awake の
増幅終了時に一度だけラッチし、設定変更や Wander 移行では解除しない。
`GameScene::DrawRender()` はこの判定で `Enemy::Draw()` と `DrawExplosion()` をまとめてスキップする。
`Enemy::Draw()` 内の破片・面破片・部位・マーカーも同時に描かれなくなる。

敵のオブジェクト・プール・HP・AI・部位の可視フラグ・破片の寿命には書き込まない。
既存のゲームオーバー時の敵更新停止処理は維持する。
演出個体の移動は元からステージ衝突だけを参照し、見えない敵には引っ掛からない。

`Reset()` が描画ラッチを解除する。既存の `OnExit()` からの Reset でシーン終了時に復旧し、
Retry / Title の遷移は暗転中にその処理を通る。新しいシーンも `OnEnter()` で Reset する。
遷移の暗転前に敵を再表示しない。敵側の描画状態を変更していないので個体別の復元は不要。

## 検証

```powershell
./Tools/build.ps1 -Configuration Debug
./Tools/test-gameover-runtime.ps1 -Configuration Debug -Stage stage03
./Tools/build.ps1 -Configuration Release
./Tools/test-gameover-runtime.ps1 -Configuration Release -Stage stage03
```

既存の死亡・閉眼・開眼・起き上がり・Possess・Wander・GPUノイズ検証に加え、
増幅ピーク、消失直前 / 直後、30 / 60 / 144 FPS、遅延フレーム、敵なし、
タイミング変更による静止延長、ポスト無効、Reset / Retry / Title / 開眼途中の退出を検証する。
3体（部位破壊済み個体・死亡破片を含む）の実描画を、敵なしのGPU画像と比較する。
消失後は敵なし画像と完全一致し、Reset 後は消失前の破損形状の画像と完全一致することを確認する。

キャプチャとログは `generated/gameover-tests/stage03/` に保存する。

### 検証結果（2026-10-27）

- Debug / Release ともビルド成功、stage03 の全ランタイムテストが PASS。
- 両構成で3体・破片の消失前と敵なし参照画像に18,102バイトの画素差を確認。
  消失後と敵なし参照画像の差は0、Reset 後と消失前の差も0。
- 消失前後の境界、増幅強度、元のノイズへの復帰、静止、30 / 60 / 144 FPS、
  遅延フレーム、敵なし、設定変更、ポスト無効、Wander 継続、Retry / Title / 途中終了からの復旧を通過。
- 既存の4描画サイズ・虹色ノイズのGPU検証、死亡〜再覚醒〜徘徊の回帰検証も通過。
- 消失専用の完全遮蔽を追加。背景がグレーの場合と色付きの縞模様の場合で、
  遮蔽中のGPU画像が完全一致することを4描画サイズで検証済み（Debug / Release とも PASS）。
  通常ノイズの強度・密度が0でも、専用遮蔽が1なら穴が生じないことを確認済み。
- 比較画像とログは `generated/gameover-tests/stage03/Debug/` と `Release/` に保存。
  `disappear-visible-three-with-debris.png` / `disappear-hidden-three-with-debris.png` /
  `disappear-restored-three-with-debris.png` で確認できる。

## 今回変更したファイル

- `Game/GameOverSequence.h`：調整項目・描画判定・ラッチ
- `Game/GameOverSequence.cpp`：タイミング、ノイズ増幅、静止時間、Reset
- `Engine/Render/DeathPostEffect.h` / `RenderManager.cpp`：消失時専用の遮蔽パラメータ
- `resources/shaders/DeathPostEffect.PS.hlsl`：短時間の完全遮蔽。通常の開眼ノイズは既存の混合率を維持
- `Game/scene/Main/GameScene.cpp`：敵と付随表示の描画制御
- `tests/GameOverSequenceRuntimeTests.h`：既存ランタイムテストへの回帰検証追加
- `tests/GameOverPostEffectRuntimeTests.h`：異なる背景で完全遮蔽画像が一致するGPU検証
- `docs/gameover-enemy-disappearance.md`：設定と検証手順

着手前から存在したカラーノイズ等の未コミット変更を保持している。
