# Enemyの部位別Hitscan

## 実装

EnemyPartTypeはHead / Body / LeftArm / RightArm / LeftLeg / RightLeg。Enemy::Raycastは命中の有無、ワールド距離、交点、部位を返します。複数の交差では最も近い部位を採用します。左右は敵自身から見た左右です。

Model::GetLocalAABBを基準にローカルAABBを6個作ります。BossのローカルYが高さ、Zが腕の広がる方向です。Xは各部位ともモデル全体の厚みを使います。以下は全体Boundsに対する割合です。

| 部位 | Y | Z |
| --- | --- | --- |
| Head | .82–1 | .38–.62 |
| Body | .45–.82 | .38–.62 |
| LeftArm | .70–.86 | .62–1 |
| RightArm | .70–.86 | 0–.38 |
| LeftLeg | 0–.45 | .50–.64 |
| RightLeg | 0–.45 | .36–.50 |

Rayを描画World行列の逆行列でローカル空間へ変換し、既存RaycastAABBで判定します。回転したBoxを大きなワールドAABBに再変換しないため、回転で不要に判定が広がりません。交差距離はワールド単位へ戻して比較し、非等方拡縮にも対応します。静止モデル用の簡易分割であり、表面や部位の輪郭への厳密な一致は保証しません。

## 表示と制約

Last Hit Partをデバッグ用のFPS Controlsに表示します。ウィンドウタイトルには表示しません。外れた場合は最後の命中部位を保持します。

Bossは頭と、胴体・四肢をまとめたメッシュで構成され、6部位それぞれを既存マテリアル設定で着色できません。大きなレンダラー変更を避け、今回の指定にあるDebug描画への代替を採用しました。モデル表面は着色せず、USE_IMGUI時のみ部位枠の色でダメージ状態を表示します。命中時は0.2秒太線にします。Releaseでは部位名・カウント・Last Damageによる確認になります。

ESCで操作を解放してFPS ControlsのShow Enemy Part CollidersをONにすると、全6部位の枠と名前を表示します。OFFでも命中枠は状態色で0.2秒だけ表示します。Enemy Transformで位置・回転（ラジアン）・拡縮を変更できます。

部位HPと射撃ダメージを追加しています。切断・モデル破壊・AIはありません。操作開始クリックを発射しない既存の射撃条件は維持しています。

## 検証

- Debug x64 / Release x64：警告0、エラー0。
- tools/test-enemy-parts.ps1：6部位、移動、回転、非等方・負スケール、ワールド距離、射程、配列順序によらない最寄り部位、外れ、特異行列を確認済み。
- Debug起動・描画確認済み。各部位を実際に撃ち分ける確認と赤枠の目視確認は未完了（操作ツールのアプリ承認タイムアウト）。
- ビルド成果物：プロジェクトの一階層上のgenerated/verified-parts/DebugおよびRelease。通常のVisual Studio出力先は変更していません。

手動確認：各部位を照準で狙ってクリックし、Last Hit Partと赤枠を確認。ESCで解放後、Transformを変更して再度確認してください。

## 部位HPとダメージ

初期HPはHead 50、Body 100、左右Arm 60、左右Leg 70。調整箇所はEnemyParts.hのEnemyPartMaxHpです。1発25（GameScene.hのkShotDamage）を命中部位だけへ適用し、HPは0で止まります。Destroyedでも描画・Raycastを維持します。

DamageRate()は1 - hp / maxHpを0～1へClampして返し、DamageState()をHPから導出します。HP割合が75%より大きければNormal（白）、75%以下LightDamage（薄赤）、50%以下HeavyDamage（赤）、25%以下Critical（濃赤）、0ならDestroyed（紫）です。Show Enemy Part CollidersをONにすると全枠の状態色を継続表示します。

FPS ControlsのEnemy Partsに全6部位の現在HP／最大HP・状態・ダメージ率を表示します。Last Damageは最後の命中で実際に減ったHPで、残り10なら10、破壊済みなら0。外れた射撃ではLast Hit PartとLast Damageを保持します。

部位HP追加の自動テストではRightArmへのRaycast→25ダメージを4回実行し、60→35→10→0→0、他5部位のHP不変、破壊済みでもRaycast命中、全5段階、無効ダメージを確認しています。今回の成果物は一階層上のgenerated/verified-part-hp/{Debug,Release}へ出力します。実操作での色の見え方は未確認です。
