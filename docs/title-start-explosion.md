# タイトル文字の爆散演出

TitleSceneのGAME START命中時に、既存の文字形状9個と小片30個を飛散させます。文字は元のHeadアセットの三角形を、同じ位置の頂点でつながった形状ごとに分けたものです。SHOOT TO STARTのように文字同士のX範囲が重なっていても個別に飛ばせます。形状・配置・当たり判定は元のアセットを使用します。

文字の初速は、全文字の中央から外側への力 × 0.8～1.2の速度差 + ランダムな力 + 上向きの力です。各文字が自身の中心を軸にXYZ方向へ回転します。小片も文字表面から外側へ飛び、Cube・細長い破片・薄い板をScaleの違いで表現します。初期値では3個だけがカメラの左右付近を通ります。この3個は小さくし、画面中央を覆わない軌道にしています。

重力・回転・寿命・床での簡単な反発はEnemyと同じ`DetachedPartMotion`を再利用します。描画には既存の`Object3d`、`ModelManager::CreatePrimitiveModel`、`GeometryGenerator::GenerateBoxTriList`を使います。文字と最大辺0.20以上の小片だけが既存Shadow Mapへ参加します。カメラ付近を通る小片は影を落としません。

命中フレームは文字を元の位置で発光させ、次の更新から飛散させます。Flashは既存Materialの色とLighting無効化による0.075秒の演出です。0.14秒の小さなカメラ位置の揺れはTitleScene内で減衰させます。毎フレームPlayerから通常のカメラ位置を復元するため、揺れが積み重なったり移動・照準へ残ったりしません。

GAME STARTへの命中から0.45秒後に既存`SceneManager::TransitionTo("Game", .75f, .75f)`を呼びます。既存のFadeOut、暗転フレームの表示、Stage01読み込み、FadeInの処理はそのままです。Enemy本体・ライト設定・レイアウト・シーン遷移のコードには変更を加えていません。

## UNALIVE

UNALIVEも1発当たると、元の文字7個と小片30個がGAME STARTと同じ飛散・Flash・Shake・Shadowの処理で四散します。背景モデルの`Game_Title`ノードから形状を取り出して使うため、新しい文字モデルやEnemyは追加しません。射撃判定は元の三角形に沿い、文字間の隙間を埋めず、壁越しの命中も防ぎます。

UNALIVEを撃ってもFadeやゲーム開始は発生せず、移動・射撃を続けられます。寿命が終わると破片が消え、UNALIVEは壊れたままになります。その後GAME STARTを撃てば通常どおりStage01へ移行します。タイトルへ入り直すとUNALIVEが復元します。

## SHOOT TO START

SHOOT TO STARTも1発当たると、元の文字12個と小片30個が同じ飛散・Flash・Shake・Shadowで四散します。背景モデルの`Start_Instruction`ノードの文字だけを使用し、文字の下にある台座は残します。判定は元の文字の三角形に沿うため、文字間の隙間や壁の遮蔽を維持します。

UNALIVEと同じく、撃ってもゲーム開始やFadeは発生しません。破片の寿命終了後も壊れた状態を保ち、UNALIVEとは独立して破壊・プレビュー・復元できます。両方を壊した後もGAME STARTを撃つと通常どおりStage01へ遷移します。タイトルへ入り直すと両方の文字が復元します。

## ImGuiでの調整

Debug / Developmentでタイトルを開き、**Esc**でマウスを解放します。既存の **Title Lighting** タブ内の **GAME START Explosion** を開いて調整してください。爆散設定はGAME START、UNALIVE、SHOOT TO STARTで共通です。Fade Start DelayはGAME STARTにだけ適用します。

| 項目 | 初期値 | 内容 |
| --- | --- | --- |
| Fragment Count | 30 | 小片の個数。0～40。GAME STARTの9文字／UNALIVEの7文字／SHOOT TO STARTの12文字は別枠 |
| Explosion Power | 7.5 | 外側への初速。文字と小片で速度差あり |
| Random Power | 2.0 | 初速へ加えるランダムな力 |
| Upward Power | 3.2 | 初速へ加える上向きの力 |
| Fragment Scale Range | 0.065～0.16 | 小片の基本サイズ。細長さ・薄さを追加で変更 |
| Angular Velocity (rad/s) | 9.0 | XYZの回転速度の上限。各軸で符号・速度をランダム化 |
| Fragment Lifetime | 1.6秒 | 文字と小片の寿命 |
| Camera Shake Strength | 0.035 | カメラの最大位置変位の基準値 |
| Camera Shake Duration | 0.14秒 | 減衰する揺れの長さ。0で無効 |
| Flash Duration | 0.075秒 | 短い発光の長さ。0で無効 |
| Fade Start Delay | 0.45秒 | 命中から既存FadeOut開始までの時間 |

**Preview Explosion (no transition)** で、射撃・シーン遷移なしで演出を繰り返せます。寿命が終わるとGAME STARTの表示が戻ります。再度押すとその時点の値で再生します。**Reset Explosion Defaults** で上記の初期値へ戻します。Scene画像をクリックすると射撃操作へ戻ります。

**Preview UNALIVE** はUNALIVEの爆散を再生し、寿命が終わると文字と射撃判定を復元します。**Restore UNALIVE** はその場でUNALIVEを復元します。実際の射撃で壊した場合は自動では復元しません。

**Preview SHOOT TO START** で同様に演出を試し、**Restore SHOOT TO START** で文字を復元できます。いずれの操作も、もう片方の文字の破壊状態は変更しません。

調整値は現在のTitleScene内だけに保持されます。**Save Lighting** はライト設定のみを保存します。爆散の初期値を恒久的に変える場合は`TitleScene.h`の`StartExplosionSettings`、Fadeの初期Delayは`title.json`の`title.explosionDelay`（0.4～0.5秒）を変更してください。Flashを確認しやすくするには一時的にFlash Durationを長くし、最後に0.05～0.1秒へ戻してください。

GPUリソースはTitleScene開始時に文字と最大40個の小片を準備し、命中時・プレビュー時に新しい描画オブジェクトを作りません。非常に小さい小片はShadow Passへ追加しません。

## 検証

`Tools/build.ps1 -Configuration Debug` / `-Configuration Release` でx64をビルドできます。`Tools/test-title-scene.ps1`で、文字形状の保持、プレビューからの復帰、命中時の生成数・発光・回転・飛散、Flash/Shake終了、手前を通る破片、影のサイズ制限、命中時のGPUオブジェクト追加なし、既存暗転→Stage01遷移を検証します。描画キャプチャは`generated/title-tests/`へ出力します。

UNALIVEについても実弾の飛行後の命中、文字間の隙間と壁による遮蔽、7文字と小片の飛散、重複命中での再起爆防止、ゲーム開始なし、寿命終了後の破壊状態保持、プレビュー復元、タイトル再入場時の復元を検証します。

SHOOT TO STARTについては12文字の形状保持、台座を含む実際のステージの衝突判定での実弾命中、隙間・壁の遮蔽、飛散・発光、GPUオブジェクト追加なし、ゲーム開始なし、重複命中防止、寿命終了後の破壊状態、UNALIVEとの独立したプレビュー・復元、両方を壊した後のGAME START遷移を検証します。
