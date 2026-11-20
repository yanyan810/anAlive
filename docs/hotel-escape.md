# ホテルの車両脱出演出

Stage03またはStage04で出口の車付近にある既存のGoalへ入ると始まります。約7秒後に `STAGE CLEAR` が表示されます。クリアタイムと撃破数はGoal到達時点の値です。車が衝突した演出用の敵は撃破数の集計対象外です。

Debug／Developmentでは、開始カメラ終了後にゲームウィンドウへフォーカスして **K** を押すと、ステージ内のどこからでも試写できます。試写中はゲーム進行が一時停止します。終了すると開始前のプレイヤー・カメラ・武器・敵の状態に戻り、車は元の位置に停車します。必要に応じてDebugの **Spawn System → Restart Stage** で再読み込みできます。

## Blenderで編集

1. `Tools/blender/yanengine_level_exporter.py` の **YanEngine Level 1.24** を有効にします。対象ホテルの `.blend` を開き、**ViewLayer** に切り替えます。Outlinerの **Hotel Escape** コレクションに、車・経路・敵・カメラのマーカーがあります。
2. **Escape_Car** を移動・回転して停車位置と初期向きを設定します。**N → YanEngine Level** にある **Goal** で対応するゴールを選びます。既定では1.2秒で発進して正面カットに切り替わり、**Cut 2 Start** の1.45秒から後方追走に入ります。6秒で暗転を開始し、7秒で終了します。
3. **Escape_Road_End** を移動して終点を設定します。途中で曲がる場合は **Hotel Escape Waypoint** を追加し、一意の **Order** を付けます。車は番号の昇順に直線で移動します。**Road Width** は再生中に描く道路の幅です。既存の道路を使う場合は `0` を指定できます。Stage03では道路終端の壁とその奥の仮構造物に **Hide During Hotel Escape** を設定しています。
4. **Escape_Enemy_1〜3** の位置と **敵定義** を編集します。再生時は停車中の車の方を向きます。**Launch Speed** で吹き飛びの強さを設定します。**Launch Direction** はBlenderのワールド座標で入力します。ゼロベクトルの場合は車の進行方向と敵が当たった側から求めます。
5. **Cut 1 Sequence ID** は空転を低い斜め正面から捉え、**Launch Sequence ID** は発進を短く正面から映します。`Escape_Cut1_*` と `Escape_Launch_*` の `*_Target` マーカー、`Duration to next`、補間方式でこの2カットを調整できます。
6. **レベルを検証 → レベルを書き出す** を実行し、ゲームでステージを再読み込みします。ゲーム用JSONには `hotelEscape` とカメラシーケンスが出力されます。

後方追走の試作は `Game/HotelEscape.h` の `ChaseCameraAt` で調整します。最初の経路の向きと演出時間からカメラ位置を計算し、衝突後は緩やかに減速しながら上昇します。Blenderの `Escape_Cut2_*` は従来の追走カメラの編集データとして保存しています。

仮車両は共通の `resources/levels/hotel_escape_car.glb` です。読み込めない場合は箱形の車で再生します。演出設定やカメラが利用できない場合はGoalから通常のクリア画面へ進みます。

## 発進モーションの調整

**Escape_Car → N → YanEngine Level → Launch Motion** から調整できます。同じ項目をステージJSONの `hotelEscape.bodyMotion` でも設定できます。

| Blender | JSON | 初期値 | 動き |
| --- | --- | --- | --- |
| Ignition (s) | `ignitionSeconds` | 0.8秒 | 停車したまま車体が揺れ、後輪が空転する時間 |
| Vibration (m) | `vibrationMeters` | 0.009 m | 始動時の上下振動の基準振幅 |
| Body Roll (deg) | `rollDegrees` | 8° | 発進前の左右の傾きの基準振幅 |
| Launch Motion (s) | `launchSeconds` | 0.84秒 | 発進時の前上がりと車高が落ち着くまでの時間 |
| Nose Lift (deg) | `pitchDegrees` | 2.5° | 発進直前の前上がり角度 |
| Body Drop (m) | `squatMeters` | 0.058 m | 発進直前に車体中心が沈む最大量 |
| Launch Boost (m/s²) | `boostAcceleration` | 42 m/s² | 発進直後にだけ加わる追加加速度の最大値 |
| Nose Rebound (deg) | `reboundDegrees` | 5° | 後部が沈んだ反動による追加の前上がり |
| Launch Roll (deg) | `launchRollDegrees` | 6.2° | 跳ね返りと横滑りに連動する横傾きの基準値 |

車体は発進前に左右へ揺れ、後輪が空転する間も車の前後・左右の位置は固定です。後部が沈んだ直後に加速度が立ち上がり、約0.1秒で前部が跳ね上がります。加速に続いて車体が横に傾き、ドリフトから逆ハンドルを切る動きに合わせて戻ります。後輪は発進をまたいで回り続け、走行速度に応じた回転へ移ります。箱形の代替モデルでは、傾けたときの下端が路面にめり込まないよう位置を補正します。`Ignition` と `Launch Motion` はそれぞれ `0` で無効にできます。

## 横滑りの調整

**Escape_Car → N → YanEngine Level → Drift** またはステージJSONの `hotelEscape.drift` から編集できます。**Acceleration** は車の既存項目で、試作の初期値は15 m/s²です。

| Blender | JSON | 両ホテルの初期値 | 動き |
| --- | --- | --- | --- |
| Side Offset (m) | `lateralMeters` | −0.8 m | 進行方向から見て負の値で左、正の値で右へ滑る。`0` で解除 |
| Slip Angle (deg) | `yawDegrees` | 22° | 実際の移動方向と車体の向きの最大角度差 |
| Slide (s) | `slideSeconds` | 0.72秒 | 発進から横滑りまでの時間 |
| Recovery (s) | `recoverySeconds` | 0.52秒 | 逆方向に少し振れてから進路中央に戻る時間 |

発進区間の経路は直線を基準に調整します。車体の向きは前進と横移動を合成した速度方向に追従し、発進後の短い間だけ横滑り角を加えます。角度差は発進約0.25秒後に最大となり、`Escape_Enemy_1` が側面に当たる頃には縮まります。車が道路中央へ戻る間は車体の向きが移動方向に揃い、直進復帰後に `Escape_Enemy_2` と `Escape_Enemy_3` に当たります。滑る方向や加速度を変えたときは、敵の配置と **Cut 2 Start** もKキー試写で確認してください。

## 動作確認

`Tools/test-upper-hotel.ps1 -Configuration Debug` と `-Configuration Release` でホテル両方のGoal、衝突、クリア画面、Restartを検証できます。DebugではKの入力判定、連打抑止、破片の生成、プレイヤーと敵の状態の復帰も確認します。画面キャプチャは `generated/upper-hotel-tests/escape-*.png` に出力されます。

Debug実行ファイルに `--upper-hotel-test --escape-launch-capture` を渡すと、Stage04のKキー試写から60fpsの連番画像を `generated/upper-hotel-tests/escape-launch-frames` に出力します。

`--upper-hotel-test --escape-camera-capture` を渡すと、Stage04のGoal到達からクリア画面までを60fpsで `generated/upper-hotel-tests/escape-camera-frames` に出力します。
