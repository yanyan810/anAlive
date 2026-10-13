# GoalとStage Clear

`resources/levels/fps_spawns.json` に省略可能な `goalTriggers` を追加しました。SpawnPoint / SpawnTriggerと同じLevel設定として扱います。既存のSpawn設定・読み込み処理は変更しておらず、goalTriggersがないJSONでは従来どおりSpawnのみが動作します。

```json
"goalTriggers": [
  { "id": "Goal_Main", "position": [3, 1, 52], "size": [12, 4, 4] }
]
```

positionはBox中心、sizeは全幅・全高・全奥行きです。Playerの足元座標を含んだときに起動します。現在の配置はSTART (3,0,-6) → A入口 Z=2 → B入口 Z=24 → Goal入口 Z=50。全滅条件はありません。

## 更新順と停止範囲

`StageProgress` はPlaying / Cleared、Goal、経過時間、撃破済みEnemy ID集合を管理します。SpawnSystemには依存しません。GameSceneがPlayer移動後にGoal判定を行い、Playingの間だけSpawn、敵の分離・AI、射撃、Playerへの攻撃を更新します。Goalに入ったフレームもこれらを実行しません。

Cleared後はPlayer移動・マウス視点・射撃・AI・攻撃・ダメージ・Spawnを停止し、カーソルを解放します。Goal到達処理は一度だけ実行します。Spawnの残数とタイマーは停止時点で保持します。`Enemy::UpdateVisuals` のみを継続するので、Face / Chunkは落下・消滅まで動作します。AIの状態表示は停止直前の値を保持します。

結果画面の **RETURN TO TITLE** をクリック、またはEnter（テンキーEnterも可）でタイトルに戻れます。DebugのScene画像とReleaseのウィンドウサイズに合わせてクリック位置を変換します。復帰は共通の0.5秒FadeOut → タイトル初期化 → 0.5秒FadeInで行い、遷移中の再要求は無視します。クリアした背景と結果はFadeOut中も保持します。

時間はPlaying中のシミュレーションdtの合計で、クリア時に固定します。アプリが更新を止める非アクティブ時間などは含みません。撃破数は初めてDeadとして観測した個体IDだけを加算し、クリア後は変わりません。結果はSpriteによって中央へ描画され、ImGuiを使用しないReleaseでも表示します。フォントは同梱の `resources/ui/stage_font.png`。再生成用スクリプトは `tools/generate-stage-font.ps1` です。

## DebugとRestart

Goalは紫色Boxと `GOAL: Goal_Main` ラベルで表示します。Stage Progressには状態、時間、撃破数、Goal中心までの距離、activatedを表示します。Spawn SystemはCleared時にスケジュール凍結を表示します。

ESCでカーソルを解放して `Restart Stage` を押すと、既存SceneManagerへGameシーンの再生成を予約します。次のUpdateで新規GameSceneとなり、Player位置・向き・HP、Enemy、Spawn/Goalの状態、各カウント、時間、射撃履歴を初期化し、JSONも再読込します。古いSceneは次フレームでGPU完了を待って解放するため、繰り返しRestartしても蓄積しません。

Debug限定の `Test positions (debug teleport)` を開くと、JSONから取得したA / B / Goalへ移動するボタンを使えます。通常の判定は次のUpdateで行われます。Cleared中はこの位置操作も無効です。通常のWASD進行と区別して検証に利用してください。

## 検証結果と手動確認

- タイトル復帰の追加検証：Debug/Releaseの実GPUテストでStage01のGoalを起動し、結果表示、タイトルボタンの範囲、遷移中の結果固定、重複要求防止、タイトル初期化とフェード完了を確認。`generated/transition-tests/clear-title-menu.png` に表示画像を保存。キーボードとマウスの手操作は下記の手動確認とは別です。
- Debug / Release x64: 警告0、エラー0。既存のDebug /Z7によるPDB対策を維持。
- `tools/test-stage-progress.ps1`: Goal境界、再入場時の一回性、固定時間と撃破数、ID重複排除、Reset、旧JSON互換性、不正設定拒否、A→B→Goal、生存敵を残したクリア、Spawn/攻撃停止、GoalとSpawn領域が重なる場合の優先順が成功。
- Spawn、Enemy部位/AI/破片運動、FPS移動、Raycastの既存テストが成功。
- 実ゲーム: Debug起動、通常シーン、Goalの紫Boxとラベル表示まで確認。Computer UseがEscapeによって停止されたため、その後の実操作は行っていません。下記は未確認です。

1. 正面へWで進み、A/Bで敵が出現すること。
2. 敵を残したままZ=50へ進み、STAGE CLEAR・TIME・ENEMIES DEFEATEDが中央に表示されること。
3. 表示後の時間・位置・HP・Shot Count・Enemy Countが変化しないこと。Bが4/8で上限待ちの状態からクリアし、残りを生成しないこと。
4. 生存敵の移動・攻撃が止まり、直前に生成したFace/Chunkだけは動作を継続すること。
5. 同じ死体への追加射撃で撃破数が重複せず、Head/Body撃破した個体数と結果が一致すること。
6. Restart Stageで開始位置、HP100、敵0、Playing、タイマー/撃破数0、A/B/Goal未起動へ戻ること。もう一度クリアできること。

Stage ProgressとSpawn Systemが重なっている場合は、ウィンドウのタイトルバーをドラッグして分けてください。
