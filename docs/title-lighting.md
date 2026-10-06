# TitleSceneのライト調整

Debug / Developmentでタイトル画面を開き、**Esc**でマウス操作を解除すると、右側のInspectorと同じ位置にある **Title Lighting** タブから編集できます。Scene画像をクリックすると射撃操作へ戻ります。

調整対象は **Directional Light / UNALIVE / Enemy / GAME START** の4項目です。変更は次の描画から反映され、Enemyの再出現後も適用されます。

| 項目 | 内容 |
| --- | --- |
| Position | Spotのワールド座標 |
| Direction | 光源から照射先への方向ベクトル。描画時に自動で正規化 |
| Color | RGB。GAME STARTを赤系にする場合もここから変更 |
| Intensity | 光の強さ。0で消灯 |
| Distance | Spotの最大照射距離。この距離で光が0になる |
| Decay | 距離減衰の指数。大きくすると近距離から暗くなる |
| Outer Angle (deg) | Spotの中心軸から外縁までの半角 |
| Inner Angle (deg) | 中心軸から角度減衰が始まる位置までの半角。Outerより小さくする |
| Highlight Strength | Spotの鏡面反射の強さ。壁の反射が強すぎる場合に下げる |

DirectionalにはDirection / Color / Intensityがあります。距離や位置を持たない平行光です。環境の最低限の形を残すために既存のHalf Lambertを利用し、Point Lightは消灯しています。Spotには既存の法線による陰影・距離減衰・角度減衰を使っています。

初期IntensityはDirectional **0.22**、UNALIVE **0.9**、Enemy **1.1**、GAME START **2.2**です。背景を暗く保ち、GAME STARTとSHOOT TO STARTを優先して照らします。

**Save Lighting**で `resources/levels/title/title_lighting.json` に保存します。保存済みの値は次回のTitleScene開始時にも読み込まれます。既存のDebugJsonEditorを使用し、保存前の検証、`.debug-backup`の作成、外部変更の検出を行います。Blender出力のtitle.jsonとは独立しているため、レイアウトの再出力で上書きされません。

**Reload Lighting**はファイルから読み直し、未保存の編集を破棄します。**Reset Defaults**は初期値に戻し、Save Lightingを押すまでファイルへは反映しません。読み込みエラー時は画面に理由を表示し、起動時は既定のライトでタイトルを表示します。

既存のObject3dLightのSpotを最大3灯へ拡張し、TitleSceneが所有するライトを環境・Enemy・GAME STARTで共有しています。他のシーンの既存Spot APIは引き続き1灯目を使い、残りの2灯は消灯します。タイトルの配置・当たり判定・射撃・爆散・開始シーケンスは従来の処理を使用します。
