# タイトルの GAME OVER デモ

## 操作

1. タイトル画面で **Esc** を押してマウス操作を解放する。
2. 画面右上の **GAME START / GAME OVER** の小さなボタンをクリックする。
   選択中のモードは金色の文字と明るい背景で示す。Debug / Release 共通。
3. 画面内をクリックして射撃操作へ戻り、立体文字 **GAME OVER** を撃つ。
   操作を戻すためのクリックは射撃として扱わず、次の射撃から有効になる。
4. 既存の死亡〜再覚醒〜徘徊をタイトル内で再生し、GAME OVER メニューを表示する。
5. **Retry**：タイトル開始位置・向き、敵、文字、表示状態を初期化してデモを再生する。
6. **Title**：GAME STARTモードの通常タイトルへ戻る。画面をクリックすると通常操作を再開できる。

Debug の ImGui **Title Lighting** にも `GAME START` / `GAME OVER demo` の選択を追加。
モード選択だけで演出は始まらない。GAME OVER の実際の文字形状への弾の命中だけで開始する。
文字の穴・隙間・壁への着弾はデモ開始にならない。
演出中は通常の射撃、タイトル移動、モード変更、F6/F7のタイトル操作を受け付けない。
メニューは従来どおり上下キー＋Enter、またはクリックで操作する。押しっぱなしの射撃・Enterは受付前に解放が必要。

## 再利用・共通化

- `GameOverSequence` の実装は複製も変更もしていない。TitleScene が同じクラスの
  Begin / Update / Draw、Vision、PostEffect、EnemiesVisible、Settings を使う。
- ステージの開始や新しいGameSceneの起動を伴わず、TitleScene内で再生する。
- 死亡の初回は現在のプレイヤー位置とカメラ姿勢から開始する。
  Retryはタイトルデータの `playerSpawn` から再生する。
- `level_.collision` の床・壁・台座を同じカメラ掃引・体の移動へ渡す。
  Wanderも既存 EnemyAI / EnemyNavigation を使用する。タイトルの壁で囲まれた移動可能領域で徘徊する。
- 再生の時間・赤いビネット・色収差・彩度・二重視界と揺れ・接地ブラー・まぶた・開眼ノイズ・
  消失時の完全ノイズ遮蔽は、同じ `gameOver_.settings` に従う。
- GAME OVER UI、まぶたは既存の `GameOverOverlay` / `GameOverVisionOverlay`。
  新しいポストエフェクトをメニューへ適用しない。
- `GameOverMenuInput.h` に既存のメニュー入力を移し、GameScene / TitleSceneの両方から使用。
  画面座標変換、マウス選択、キーボード選択、押しっぱなし防止を共通化した。
  選択後の処理は各シーンが決めるため、通常GameSceneのRetryは従来どおりステージの再開。

## 文字と消失対象

GAME OVER の立体メッシュは既存タイトルの GAME START / SHOOT TO START / UNALIVE の
同じフォント・ベベルの文字を組み合わせ、GAME STARTの位置・全体幅・高さ・奥行きに合わせた。
新しいフォント画像やステージの再エクスポートは不要。描画した三角形から射撃判定も生成する。
モード切り替えは準備済みの描画と射撃判定を同時に切り替え、飛行中の弾をクリアする。

通常のGAME STARTは元のEnemy型のHead/1HPターゲット、破壊演出、0.45秒の待ちと
既存のStage03へのフェード遷移を維持する。
GAME OVERは独立したシーン所有の射撃ターゲットとし、GAME STARTのHPや破壊を発生させない。
弾の処理中に配列を破壊しないよう、命中要求を受けた後、そのフレームの弾更新終了時にデモを開始する。

描画・影の両方で、消失対象は **通常敵（startTarget_以外）** に限定する。
ステージ、UNALIVE、SHOOT TO START、GAME OVERは消さない。敵のHPや部位の可視フラグを変更せず描画だけを止める。

## Retry / Title / シーン終了

Retry / Title はGPU完了を待ってから演出用ボディや敵の破片を初期化する。
プール内の既存スロットを再利用し、シーンの再生成やプール拡張は行わない。
カメラの位置・向き・Near/FOV、HP、移動可否、武器、タイトル文字、通常敵、ライトの明滅状態、
メニュー選択と押下受付、弾、ポストエフェクト、プレビュー状態を復旧する。
演出設定は維持する。Titleを選択するとGAME STARTモードへ戻る。
OnExitでもGameOverSequenceとポストエフェクト・描画ラッチをリセットする。

## 今回の変更ファイル

| ファイル | 内容 |
| --- | --- |
| `Game/scene/Flow/TitleScene.h` / `.cpp` | モード、UI、文字の描画・命中先切り替え、通常操作とデモの分岐、敵と影の消失 |
| `Game/scene/Flow/TitleScene.Demo.cpp`（追加） | 既存文字からのデモ文字準備、既存シーケンスの呼び出し、Retry / Title復旧 |
| `Game/GameOverMenuInput.h`（追加） | 両シーン共通のメニュー入力と画面座標変換 |
| `Game/GameOverOverlay.h` | リソースを作り直さず選択・押下受付をResetする関数 |
| `Game/scene/Main/GameScene.cpp` | 既存のメニュー入力を共通関数に置換 |
| `tests/TitleGameOverDemoTests.h`（追加） | Debug / Release共通の実弾・シーケンス・GPU画像・復旧・通常開始の検証 |
| `Tools/test-title-gameover-demo.ps1`（追加） | 検証起動と構成別キャプチャ保存 |
| `Engine/Core/GameApp.cpp` / `Engine/Input/Input.h` | 専用テスト起動・入力検証のfriend宣言 |
| `CG2_Setup.vcxproj` | 追加ソース・ヘッダーの登録 |
| `docs/title-gameover-demo.md`（追加） | 操作・構造・検証の記録 |

既存の未コミット変更を保持している。敵AI・武器定義・ステージ資産は変更していない。

## 検証

```powershell
./Tools/build.ps1 -Configuration Debug
./Tools/test-title-gameover-demo.ps1 -Configuration Debug
./Tools/test-title-scene.ps1
./Tools/test-gameover-runtime.ps1 -Configuration Debug -Stage stage03
./Tools/build.ps1 -Configuration Release
./Tools/test-title-gameover-demo.ps1 -Configuration Release
./Tools/test-gameover-runtime.ps1 -Configuration Release -Stage stage03
```

2026-10-27：Debug / Releaseビルド成功。両構成のタイトルデモ検証がPASS。

- モード変更で立体文字が変わり、切り替えただけでは演出が始まらない。
- 弾の命中でタイトル内の死亡演出を開始。空振り・壁越しでは開始しない。
- 死亡時の二重視界・赤いビネット・接地ブラー、完全な黒いまぶた、開眼後ノイズをGPU描画。
- ノイズの完全遮蔽を挟んで通常敵の表示を切り替える。
  敵が消えた画像は「通常敵のみを取り除いた参照画像」と全画素一致し、影も残らない。
- カメラ・Wanderの20秒間の移動がタイトルColliderを貫通しない。
- Retryを3回繰り返しても同じTitleSceneのままで、プール容量・実行時割当数は増えない。
- Titleで通常モード・カメラ・HP・敵・文字・ポストを復旧。
- 復旧後のGAME STARTへの実弾命中で元の破壊演出を再生し、Stage03への通常遷移が完了する。
- 既存のタイトル全体の破壊・照明・影・通常ゲーム開始のDebug回帰テストもPASS。
- 既存GameSceneのゲームオーバー／Retry／Title／途中退出／ポストエフェクトの
  全ランタイム・GPU回帰テストもDebug / Release両構成でPASS。

キャプチャは `generated/title-demo-tests/Debug/` と `Release/` に保存。
`00-game-start-mode.png` / `01-game-over-mode.png` がモード比較、
`02`〜`09`が死亡演出、`10-title-restored.png`がTitle復旧後の画面。
