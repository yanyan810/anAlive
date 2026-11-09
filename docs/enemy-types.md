# 敵タイプ拡張

共通EnemyにEnemyDefinitionを適用し、Normal / Fast / Tankは既存近接AI、Ranged / Bomberは距離維持AIを使用します。敵IDによる行動分岐はありません。部位破壊・ダメージ状態色・HeadまたはBody破壊による死亡を維持しています。本番UIは変更していません。

## 調整

- `resources/Data/enemies.json`：5種類の性能。hpMultiplierは全6部位のhp/maxHpに適用します。
- `resources/levels/fps_spawns.json`：各SpawnPointのenemyPool。初期重みはNormal 5 / Fast 2 / Ranged 2 / Tank 1 / Bomber 1です。pool省略時はnormal。明示された空poolや全weight=0は読み込みエラーです。
- 任意のルート設定 `"enemyRandom": { "useFixedSeed": true, "seed": 12345 }` で敵スポーン乱数を固定できます。同一の更新・スポーン順序で再現します。武器の乱数とは独立しています。
- 定義とスポーンのロードはそれぞれ検証完了後に置き換えます。失敗時は直前の有効状態を維持します。シーン初期化時に敵定義が不正なら敵スポーンを開始せず、デバッガへエラーを出力します。
- Rangedは `minRange` 未満で後退を始め、`preferredRange` まで距離を取ります。`maxRange` を超えると接近します。
- 通常弾は水色、爆弾は大きめのオレンジ色Cubeです。通常弾は発射時の方向へ直進し、移動区間とプレイヤーの簡易球で衝突判定します。
- 爆弾は発射時のプレイヤーXZ位置へ着地する初速を設定し、重力で落下します。地面Y=0で停止後、fuseTimeを消費して範囲ダメージを1回与え、消えます。直撃ダメージはありません。爆発パーティクル・SEは追加していません。
- 弾の寿命は発射時から数えます。爆弾も寿命到達時には削除されるため、調整時は飛行時間＋fuseTimeより長いprojectileLifetimeを設定してください。
- 発射元が死亡しても弾は残り、StageClearとシーン終了時には弾・描画オブジェクトを全消去します。

## 検証結果

Debug / Release x64ビルド：両方とも警告0・エラー0。

以下の自動テストが成功しています。ゲーム画面の操作による確認は実施していません。

- tools/test-enemy-types.ps1：5タイプの読み込み、不正値・重複・不明ID/type拒否とトランザクション、全部位HP倍率・死亡、距離制御・射撃間隔、弾の移動・寿命・連続衝突・一度だけのダメージ、爆弾の重力・着地・Fuse・範囲内外ダメージ、固定Seed・重み付き抽選・旧形式fallback、弾のクリア。
- tools/test-enemy-parts.ps1：既存の部位破壊・近接AI・破片・複数敵。
- tools/test-enemy-spawn.ps1：既存スポーン。
- tools/test-stage-progress.ps1：既存ステージ進行。
- tools/test-weapons.ps1：既存武器。
- tools/test-fps.ps1：既存移動。
- tools/test-raycast.ps1：既存射線。

## 手元確認チェックリスト

抽選なので1回のプレイで全種類が出る保証はありません。特定タイプを確実に確認するには、対象SpawnPointのenemyPoolをそのID・weight=1の1件に一時変更してください。Debugの敵一覧で実際のタイプを確認できます。

- [ ] Normalがプレイヤーに接近して近接攻撃する。
- [ ] FastがNormalより速く、HPが低い。
- [ ] TankがNormalより硬く、移動が遅く、近接攻撃が強い。
- [ ] Rangedが一定距離を取り、水色の見える弾を撃つ。
- [ ] Rangedへ近づくと後退する。
- [ ] Ranged Projectileに当たるとPlayer HPが減り、弾が消える。
- [ ] Bomberのオレンジ色のBombが山なりに飛ぶ。
- [ ] Bombが地面で停止する。
- [ ] 着地後のFuseが経過すると爆発し、Bombが消える。
- [ ] 爆発範囲内ならPlayer HPが減り、範囲外なら減らない。
- [ ] enemyPoolから複数種類がSpawnする。
- [ ] Head / Body破壊で各タイプとも死亡する。
- [ ] StageClear後にProjectileが残らない。

## 体格と3D Marker

`enemies.json`の`visualScale`をEnemyDefinition.visualScaleMultiplierへ読み込み、Enemyの基本scale_へ成分ごとに掛けます。基本値2.0ならNormal 1.30、Ranged 1.24、Fast 1.04、Tank 1.64、Bomber 1.36です。基本scale_自体は変更しないため、定義の再適用で倍率が累積しません。

本体と6分割モデルは同じ実効Scaleを使います。EnemyPartsはモデルのローカル座標を維持し、Raycast・Debug境界・破片・Face Shatterが実効Scaleを含むWorld変換を使用します。

`typeMarker`でenabled / offset / scale / colorを調整できます。offsetとscaleは敵モデルのローカル座標です。Markerの位置と大きさには敵の実効Scaleを適用し、向きも敵のrotationへ追従します。Ranged/Fast/Bomberは頭上、Tankは肩付近に配置しています。Normalは無効です。フィールド省略時は倍率[1,1,1]・Markerなしとなり、旧JSONも読み込めます。不正な配列、0以下の倍率・Markerサイズ、RGB範囲外の色はロード時に拒否します。

Markerは独立したObject3dで、DamageStateの色更新、EnemyParts、射撃判定、部位破壊・破片処理には含まれません。死亡したフレームから描画しません。Markerの背後に本体がある場合、射撃は本体に命中します。

### 見た目の手元確認

- [ ] Normal：標準サイズ、Markerなし。
- [ ] Ranged：青Marker。
- [ ] Fast：明確に小さい体格、黄色Marker。
- [ ] Tank：明確に大きい体格、肩付近の紫Marker。
- [ ] Bomber：オレンジMarker。
- [ ] 被弾時は本体だけがDamageStateに応じて赤くなり、Markerの色と競合しない。
- [ ] Markerだけを狙った射撃は命中せず、EnemyPartsだけが当たり判定を持つ。
- [ ] 移動・回転・Scale変更にMarkerが追従し、死亡時に消える。
- [ ] 各体格で射撃判定と部位破壊・Face Shatterの位置が表示と一致する。

今回もゲーム画面の操作確認は行っていません。新規の体格・変換・Marker設定検証を含め、既存の7テストスイートとDebug / Release x64ビルドを実行しました。

## 一時的な全タイプ確認用スポーン

最初のTrigger_Aは5地点をRoundRobinで巡回し、Normal → Ranged → Fast → Tank → Bomberを各1体、0.5秒間隔で必ず出します。maxAliveは5なので、先に敵を倒す必要はありません。最初のエリアへ入ってから2秒で全種類が出揃います。Trigger_Bは従来の重み付き抽選です。

## 移動用の衝突サイズ

`collisionRadius`と`collisionHeight`はワールド単位です。Stageとの移動判定、敵同士の分離、Debug円柱が同じ値を使用します。visualScaleを自動乗算しないので、見た目と移動のしやすさを別々に調整できます。射撃用EnemyPartsはこれらの値を使わず、引き続き見た目の実効Scaleへ追従します。

|Type|visualScale（XYZ共通）|collisionRadius|collisionHeight|
|---|---:|---:|---:|
|Normal|0.65|0.36|3.22|
|Ranged|0.62|0.34|3.08|
|Fast|0.52|0.29|2.58|
|Tank|0.82|0.45|4.07|
|Bomber|0.68|0.37|3.37|

高さの初期値は新しい体格に合わせ、半径も以前の移動判定の比率を目安に設定しています。両項目は有限かつ0より大きい値が必要です。省略した旧JSONでは半径0.55、高さ4.96を使用します。

Enemy同士は半径の合計を最低距離として対称に押し分けます。既存の緩やかな分離速度を維持するため、重なってSpawnした場合に1フレームで完全分離するとは限りません。壁際や混雑でも分離移動がStage Colliderを通過しないよう判定します。

DebugのSelected Enemyで対象を選ぶとVisual Scale、Effective Model Scale、Collision Radius、Collision Heightを確認できます。Show Enemy Movement ColliderをONにすると、足元から高さまでの移動用円柱を緑色で描画します。Show Enemy Part Collidersは従来どおり射撃用です。円柱は死亡時に非表示となります。Stage判定はこの円柱をBoxの軸へ保守的に投影するため、角では円柱より少し手前で止まる場合があります。
