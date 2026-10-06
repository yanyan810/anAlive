# TitleSceneのライト調整

Debug / Developmentでタイトル画面を開き、**Esc**でマウス操作を解除すると、右側のInspectorと同じ位置にある **Title Lighting** タブから編集できます。Scene画像をクリックすると射撃操作へ戻ります。

調整対象は **Directional Light / UNALIVE / Enemy / GAME START** の4項目です。変更は次の描画から反映され、Enemyの再出現後も適用されます。

| 項目 | 内容 |
| --- | --- |
| Position | Spotのワールド座標 |
| Direction | 光源から照射先への方向ベクトル。描画時に自動で正規化 |
| Color | RGB。GAME STARTを赤系にする場合もここから変更 |
| Intensity | 光の強さ。0で消灯 |
| Ambient Fill | Directional内の最低限の環境光。法線の向きや投影影によらず、床や壁の色を薄く残す |
| Distance | Spotの最大照射距離。この距離で光が0になる |
| Decay | 距離減衰の指数。大きくすると近距離から暗くなる |
| Outer Angle (deg) | Spotの中心軸から外縁までの半角 |
| Inner Angle (deg) | 中心軸から角度減衰が始まる位置までの半角。Outerより小さくする |
| Highlight Strength | Spotの鏡面反射の強さ。壁の反射が強すぎる場合に下げる |

DirectionalにはDirection / Color / Intensity / Ambient Fillがあります。距離や位置を持たない平行光です。直接光には既存のHalf Lambertを利用し、影の中にも形を残すためにAmbient Fillを加算します。Ambient Fillの初期値は **0.08**で、影の強さやDirectionalのIntensityとは独立しています。Point Lightは消灯しています。Spotには既存の法線による陰影・距離減衰・角度減衰を使っています。

初期IntensityはDirectional **0.22**、UNALIVE **0.9**、Enemy **1.1**、GAME START **2.2**です。背景を暗く保ち、GAME STARTとSHOOT TO STARTを優先して照らします。

## ランプの点滅

UNALIVEとGAME STARTのSpotには、接触不良のランプのように不規則に消灯・再点灯する演出を追加しています。SHOOT TO STARTと台座もGAME STARTのSpotで照らされるため、一緒に暗くなります。初期設定は2.5～6秒点灯した後、0.04～0.12秒の消灯を3回繰り返す短い点滅です。点滅中の再点灯は0.06～0.16秒で、ライトごとに独立したタイミングを使います。EnemyのSpotは初期状態で点滅OFFです。

**Title Lighting → UNALIVE / Enemy / GAME START** の各項目で調整します。

| 項目 | 内容 |
| --- | --- |
| Lamp Flicker | 点滅のON／OFF。OFFにするとその場で通常の明るさへ戻る |
| Flicker Interval (s) | 点滅が終わってから次の点滅まで点灯する時間のMin／Max |
| Off Duration (s) | 一度消灯する時間のMin／Max |
| Burst Flashes | 一度の点滅で消灯する回数。1～6 |
| Off Brightness | 消灯中の明るさ。0でそのSpotを完全に消灯、1で通常の明るさ |

**Save Lighting**で点滅設定も保存し、**Reload Lighting**で読み直せます。通常点灯時のIntensityは元の値を保持し、消灯中に保存してもIntensityは0に変わりません。点滅で変えるのは各SpotのIntensityだけです。Directional LightとAmbient Fillは残るため、光が消えても部屋がすべて真っ暗になるわけではありません。点滅設定のない既存JSONでは従来の常時点灯を維持します。

## Directional Shadow Map

**Directional Shadow Map**を展開すると、**Enable Shadow / Room Casts Shadows / Shadow Strength / Depth Bias / Shadow Center / View Size / Light Distance / Near Clip / Far Clip**を調整できます。これらもSave Lightingで同じJSONへ保存されます。

**Shadow Strength**の初期値は **0.6**です。**Room Casts Shadows**は初期状態でOFFにし、浅いライト角度で壁から部屋全体へ大きな影が伸びるのを防ぎます。OFFでも床・壁は文字やEnemyの影を受けます。ONにすると部屋自身も投影影を落とします。部屋の判定にはBlender出力の`Backdrop` / `Floor`で始まるノード名を使います。Ambient Fillと併せて調整すると、背景の暗さを保ちながら黒つぶれを抑えられます。

通常描画の前に、Directional Lightの視点で2048×2048の深度テクスチャへ環境・Enemy・GAME STARTを描きます。このパスにはPixel Shaderとカラー出力がありません。通常描画ではワールド座標を同じライト行列で投影し、保存された深度と比較します。3×3 PCFで影の外縁を滑らかにします。破壊された部位は深度描画から除外し、残った部位と飛散中の破片を描きます。

View Sizeは正投影の幅・高さです。小さくすると狭い範囲の影が細かくなります。Shadow Centerは描画範囲の中心、Light Distanceはその中心から仮想ライトカメラまでの距離です。Near / Far Clipはそのカメラからの深度範囲です。Depth Biasを増やすと自己影の縞模様を抑えられますが、増やしすぎると影が物体から離れます。

影はDirectional Lightの直接光に適用します。Ambient FillとSpot Lightは独立して加算されるため、Spotの強い場所では影が控えめです。今回の黒つぶれ対策では、保存済みSpot Lightの位置・色・明るさとDirectionalの向き・Intensityを保持しています。他のシーンではこの深度パスを実行せず、Ambient Fillも既定で0です。

**Save Lighting**で `resources/levels/title/title_lighting.json` に保存します。保存済みの値は次回のTitleScene開始時にも読み込まれます。既存のDebugJsonEditorを使用し、保存前の検証、`.debug-backup`の作成、外部変更の検出を行います。Blender出力のtitle.jsonとは独立しているため、レイアウトの再出力で上書きされません。

**Reload Lighting**はファイルから読み直し、未保存の編集を破棄します。**Reset Defaults**は初期値に戻し、Save Lightingを押すまでファイルへは反映しません。読み込みエラー時は画面に理由を表示し、起動時は既定のライトでタイトルを表示します。

既存のObject3dLightのSpotを最大3灯へ拡張し、TitleSceneが所有するライトを環境・Enemy・GAME STARTで共有しています。他のシーンの既存Spot APIは引き続き1灯目を使い、残りの2灯は消灯します。タイトルの配置・当たり判定・射撃・爆散・開始シーケンスは従来の処理を使用します。
