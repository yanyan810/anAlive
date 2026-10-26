> 以下は全体AABB射撃を導入した時点の記録です。現在は6部位判定とDebug赤枠へ変更しています。最新仕様・検証結果は [FPS_ENEMY_PARTS.md](FPS_ENEMY_PARTS.md) を参照してください。

# FPS Hitscan射撃の最小土台

- InputのIsLeftMouseTriggerを使う1クリック1発の射撃。長押し連射なし。
- GameSceneでキャプチャ変更前の状態を保存し、変更前後とも操作中の場合だけ発射。
  Sceneの操作開始／復帰クリック、ESCと同時のクリック、非フォーカス時は発射しない。
- Player更新後のCamera位置から、CameraのWorld行列の前方向へ射程100のRayを送る。
- Enemy::Raycastは距離と命中位置を返す。GameSceneはHitBoxのサイズを知らない。
- Engineの既存AABB型を使い、Raycast.hにslab方式の交差判定を追加。
- Bossの仮HitBoxはModel::GetLocalAABBで読み出したモデル全体の境界。8頂点をObject3dの描画World行列で変換してワールドAABBを作る。
  手置き中心・サイズによるずれを解消。現在の静止バインドポーズ向けで、メッシュの輪郭や腕の隙間には一致しない。
  将来はEnemy::Raycast内部を部位ごとの判定へ置き換える。
- 命中はカウントと0.2秒の赤フラッシュだけ。元の色へ戻る。HPやダメージはない。
- カウントはデバッグ用のFPS Controlsに表示。ウィンドウタイトルは「アンアライブ」に固定し、デバッグ情報を表示しません。画面中央には既存Spriteで照準を表示し、射撃方向を明示。

## ビルド・自動テスト

Debug x64 / Release x64: 警告0、エラー0。
`tools/test-raycast.ps1`: 命中／外れ、平行、背後、射程、境界、内部開始、方向正規化、無効入力を確認。
`tools/test-fps.ps1`: 既存FPS移動テストも成功。

## 実操作の確認項目

1. ESCで解放した後、SceneをクリックしてもShot Countが変わらない。
2. 操作中のクリックでShot Countが1だけ増え、長押しでは増え続けない。
3. Bossの仮AABB内を狙った時だけHit Countが増える。
4. 命中時は短時間赤くなって元の色へ戻る。
5. WASDとMouse Lookが動き、ESCでカーソルを解放できる。

commit / pushは実行していない。

## 実操作の確認結果と残件

初版のDebug実機でカウンター増加と、外れ時にShotのみ増えることを確認。
ユーザーから右腕付近の判定ずれの報告があり、手置きBoxをモデル境界方式へ修正。
回転・拡大・原点からずれたモデル境界の自動テストを追加して成功。
その後、ユーザーの物理ESCキーでComputer Useが停止されたため、修正版の実操作再確認は未実施。
実行中の旧バイナリを上書きしないよう、最終ビルドは一階層上のgenerated/verified-shooting/DebugおよびReleaseへ出力。
通常のVisual Studioビルド出力先は変更していない。
