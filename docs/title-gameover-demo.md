# タイトルから開始する GAME OVER デモ

## 操作

1. このブランチではタイトルの初期表示は GAME OVER。右上のモード選択ボタンは表示しない。
2. 画面内をクリックして射撃へ戻り、立体文字 GAME OVER を撃つ。
   操作復帰のクリックは発砲せず、次のクリックから射撃する。
3. 既存の黒フェードで専用ステージへ移動する。操作せずに敵の攻撃で死亡演出が始まる。
4. 演出後の Retry は専用ステージを初期状態から再実行する。
   Title は GAME OVER を初期選択したタイトルへ戻る。

Debug / Release 共通で右上の選択UIと Esc の選択案内を削除した。
ImGui の Title Lighting の切り替えは検証用として維持する。
文字の実際の三角形への命中で開始し、空振り・壁越しは開始しない。
メニューは従来どおり上下キーと Enter、またはマウスで操作する。

## 前回方針から変更した点

TitleScene の GameOverSequence、死亡カメラ更新、演出用ボディ、まぶた・メニュー描画、
タイトル内の Retry / Title リセット処理を削除した。タイトル専用の徘徊処理は不要になった。
GameOverSequence 本体や通常ゲームの演出には変更を加えていない。
GAME START の通常開始・文字の破壊・Stage03 への遷移は従来どおり。
文字切り替え、精密な命中判定、ImGui UI は再利用する。
GAME OVER の文字はワールド空間に固定し、カメラ移動・回転・揺れの反映後に毎フレーム
描画行列を更新する。GAME START 選択中も更新して、切り替え直後の位置ずれを防ぐ。

## 専用ステージ

`resources/levels/gameover_demo/` に以下を追加した。

- `gameover_demo.blend`: Blender Level Exporter の役割・出現設定を持つ編集用ファイル
- `gameover_demo.json`: 既存 StageLoader / EnemySpawnSystem が読む通常形式のステージデータ
- `gameover_demo.gltf` / `.bin`: 12m 四方の床と四方の壁のモデル

作成スクリプトは `tools/blender/create_gameover_demo.py`。
Blender の背景実行で再生成でき、保存済み blend から通常の Level Exporter で再出力も可能。
既存 stage01 / stage02 / stage03 および敵・武器の定義は変更していない。

プレイヤーは原点、通常敵6体を半径1.3mの円周に60度間隔で配置する。
各地点は StageStart / normal / 1体 / 初回待機0秒。既存スポーンシステムと敵プールを使用する。
敵同士の間隔は1.3m、敵半径は0.36mで重ならない。
プレイヤー半径0.4mとの合計0.76mより離れ、通常近接攻撃範囲1.5m以内に置く。
視線と身体の経路を遮る室内障害物はない。
EnemyAI の正面は `rotation.y - pi/2` なので、プレイヤーへの方位に pi/2 を加えた向きで配置する。
通常の視野・認識待ち時間0.35秒・攻撃力10・攻撃間隔は変更していない。

GameScene の stageId が `gameover_demo` の場合に限り、HP20、武器未装備、
武器取得・射撃無効、移動無効とする。視点操作は可能。
カメラ開始演出はデモでは強制再生も含めて抑止する。ステージデータに武器・ゴールは配置しない。
死亡は敵からの通常の ApplyDamage → HP0 → BeginGameOver の経路を使用する。
強制死亡タイマーは追加していない。

## 再利用する演出とメニュー

通常の GameScene が GameOverSequence / GameOverOverlay / GameOverVisionOverlay を所有し、
通常と同じ被弾方向・カメラ姿勢を死亡コンテキストへ渡す。
敵消失、虹色ノイズ、二重視界、接地ブラー、まぶた、再覚醒と徘徊はそのまま動作する。
Retry は既存の LeaveGameOver が実際の stageId を保持した新しい GameScene を作るため、
デモでも通常ゲームでも従来の共通処理を利用する。Title は新しい TitleScene の初期値に戻る。

## 検証コマンド

```powershell
./tools/build.ps1 -Configuration Debug
./tools/test-title-gameover-demo.ps1 -Configuration Debug
./tools/test-title-scene.ps1
./tools/test-gameover-runtime.ps1 -Configuration Debug -Stage stage03
./tools/build.ps1 -Configuration Release
./tools/test-title-gameover-demo.ps1 -Configuration Release
./tools/test-gameover-runtime.ps1 -Configuration Release -Stage stage03
```

専用テストは実際の文字命中・シーン遷移・敵AI・近接攻撃・GPU描画を通す。
HP20/未装備/6体/非重複/経路、押しっぱなし入力、開始カメラ抑止、3回のフルRetry、
Title復旧、GAME STARTからの通常開始、stage01/02/03へのデモ設定非混入を検証する。
キャプチャは `generated/title-demo-tests/Debug/` と `Release/`、
自然死亡までのゲーム内秒数と攻撃回数は各ディレクトリの `trace.txt` に記録する。

## 検証結果（2026-10-27）

- 最終ソースの Debug / Release ビルド成功（両構成とも警告0、エラー0）。
- 専用デモの実行・GPU描画テストは両構成で PASS。
- 通常敵6体すべてが視野内・遮蔽なしでプレイヤーを認識し、通常攻撃を実行。
  初回と3回のRetryすべて、両構成とも操作開始からゲーム内0.35秒で自然死亡した。
  シーン遷移のフェード・ロード時間はこの秒数に含まない。
- HP20、武器未装備、武器取得・射撃・移動の抑止、開始カメラ抑止、敵配置非重複・経路を確認。
- 二重視界・接地ブラー・全黒の閉眼・虹色ノイズ・ノイズ中の敵消失・徘徊とメニューを確認。
- 3回の専用ステージ再読込、TitleでGAME OVERへ復帰、通常GAME STARTからStage03へ到達を確認。
- stage01 / stage02 / stage03でHP100・通常初期武器・通常移動を維持。
- 既存タイトルの破壊・照明・影・通常開始・フェードの回帰テスト（Debug）は PASS。
- 既存GameSceneのゲームオーバー・Retry・Title・途中退出・カメラ衝突・
  ポストエフェクトのGPU回帰テスト（Debug / Release）は PASS。
- デモ開始前の敵描画にも開始カメラを反映し、フェード中の最初のフレームを画像で確認。
- git diff で既存3ステージ・敵定義・武器定義に変更がないこと、および diff --check 成功を確認。
