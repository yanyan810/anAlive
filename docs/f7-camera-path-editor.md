# F7 Camera Sequence：線・Bezier編集

既存のF7エディターとCameraSequenceを拡張しています。BlenderのステージJSON・blend・モデル・当たり判定は編集しません。専用オーバーライドの保存方式とPlayer VisualのYaw設定は従来どおり利用します。

## 最短の操作手順

1. **F7 → Camera Sequence → Sequence ID: start** を選びます。
2. **Draw Path** を選びます。自動的にTop表示になります。**View Settings → Draw Height (Y)** で高さを指定し、右画面を左クリックして経路を描きます。
3. **Enter / Finish** で選択IDの経路を置き換えます。確定前の **Esc / Cancel** は既存経路を保持して描画を取り消し、**Backspace / Undo last** は最後の描画点だけを取り消します。`start`のPlayerEye終端は保持します。
4. **Edit Path** で線をクリックして区間を選択します。**Segment Settings → Path Type** でLinear／Bezierを切り替えます。Bezier区間は紫、Linearと従来区間は水色、選択区間は黄色です。
5. ポイントをドラッグして移動し、オレンジの四角いハンドルをドラッグして曲線を調整します。TopではY固定、FrontではZ固定、SideではX固定です。FreeではXYZギズモで調整できます。
6. 線の途中をクリックして **Insert Point**、または線をダブルクリックします。画面に最も近い曲線位置を探して挿入します。Bezierはde Casteljau法で分割し、曲線形状を維持します。挿入直後のポイントもそのまま編集できます。
7. **Front / Side** で高さ・奥行きを調整し、**Edit Camera** で回転・FOV・Look At設定を編集します。**FollowTangent** は実際の経路の接線方向を向きます。
8. **Preview → Play / Pause / Resume / Restart / Time (s)** で確認します。**Stop** で編集視点と投影設定を復元します。
9. 左パネルをスクロールし、**Save camera only** で選択IDを保存します。**Reload cameras** で再読み込みできます。

既存のポイント一覧、追加／削除／複製／順序変更、Free／FPS／Top、Tabでの視点操作、WASD・Q/E、Fでの注視も維持しています。**Add from View** は編集カメラの位置・向き・FOVからポイントを作り、選択ポイントの直後（終端の前）へ追加します。

## 操作の補足

| 設定・操作 | 動作 |
|---|---|
| Draw Path | 確定まで別の描画ドラフトとして保持。Enterで1件のUndo操作として適用 |
| Edit Path | 点・区間・ハンドルを選択し、経路形状を編集 |
| Edit Camera | 点の位置、回転、FOV、Manual／LookAtPlayer／LookAtTarget／FollowTangentを編集 |
| Auto | 前後の点からハンドルを自動計算。ドラッグするとAlignedへ切り替え |
| Aligned | 操作したハンドルと反対側を一直線・逆方向に維持。反対側の長さは保持 |
| Free | 入・出ハンドルを独立して操作 |
| Handle In / Out offset | ポイントに対する相対座標。ポイント移動時もハンドルが追従 |
| Duration | 次の点へ移動する秒数。終端は0、途中は0.001秒以上 |
| Constant | 区間内で一定の進行速度。Bezierは弧長テーブルを使用 |
| EaseIn / EaseOut / EaseInOut | 速度の加減速。経路形状は維持 |
| Segment parameter | 選択区間の曲線パラメータ。挿入位置を数値でも指定可能 |
| Preview at selection / Alt+線クリック | 選択した曲線位置の時刻へシーク |
| Show path | 経路・点・ハンドル・方向矢印・Preview位置マーカーをまとめて表示／非表示 |
| ホイール / 中ドラッグ | Top／Front／Sideのズーム／パン |
| Undo / Redo、Ctrl+Z/Y | カメラドラフトの直近100状態。連続ドラッグは1件。描画中のCtrl+Zは最後の点を削除 |

形状は区間のPath Type、時間方向はEasingとして分離しています。従来のCatmullRom／Linear／SmoothStep／Holdは **Legacy interpolation** として保持し、未編集データの評価結果は変更しません。従来CatmullRom区間へ挿入した場合は、対象と影響する隣接区間の多項式をBezierの制御点へ変換して形状を保持します。

分割時は区間の合計Durationを維持し、挿入時刻で時間を分配します。各分割区間はそれぞれEasingを持つため、分割後の速度の推移は必要に応じて調整してください。両側で0.001秒を確保できない位置には挿入せず、理由を表示します。

FollowTangentはBezierの解析的接線を利用し、接線長がほぼ0の場合は保存されたManual回転にフォールバックします。PlayerEye終端は引き続き実際のFPS開始姿勢へ一致させます。

編集中にHoldの隣接姿勢が不一致になったりDurationが一時的に不正になっても、経路全体を隠さず、オレンジの仮表示として点と曲線を維持します。Holdの離れた2点には編集用の接続線を表示します。この仮表示は保存データを変更せず、再生・保存の検証も緩めません。問題の内容はPreview欄とビューポート上に表示します。Holdを維持する場合は両点を同じ姿勢に戻し、移動区間にする場合は対象区間をLinear／Bezierへ変更してください。

Bezierの弧長テーブルは区間ごとに256分割して保持します。評価・シーク・毎フレームの表示で再生成せず、位置・形状・ハンドル・PlayerEye位置などの形状情報が変わった際に再計算します。Duration・FOV・Easingだけの変更では再利用します。F7の評価器もドラフト単位でキャッシュし、ゲームと同じ評価処理を呼びます。

## 保存仕様

保存先は従来どおりです。

```
resources/levels/<stage>/<stage>.camera_sequences.json
```

ルートの `version: 1`、`stage`、`cameraSequences` は維持します。ポイントに以下の任意フィールドを追加します。

```json
{
  "id": "CameraPoint_1",
  "order": 0,
  "position": [29, 14, 16],
  "rotation": [0, 0, 0],
  "duration": 2,
  "interpolation": "Linear",
  "rotationMode": "FollowTangent",
  "fov": 60,
  "playerEye": false,
  "pathType": "Bezier",
  "easing": "EaseInOut",
  "handleType": "Aligned",
  "handleIn": [-1, -1, 0],
  "handleOut": [1, 1, 0]
}
```

- `pathType`省略時は従来のinterpolation評価を利用します。
- 明示的なpathTypeはLinear／Bezier。EasingはConstant／EaseIn／EaseOut／EaseInOutです。
- handleIn／handleOutはワールド軸の相対オフセットです。入側ハンドルは前区間の制御点、出側ハンドルは次区間の制御点になります。PlayerEyeにも相対ハンドルを保存できます。
- handleType省略時はAuto。Aligned／Freeには両ハンドルを保存します。Alignedの非整列データは検証エラーにします。
- 回転の保存単位は従来どおりラジアン、UI入力は度数法です。

StageLoaderとCameraSequenceOverridesの既存処理を再利用し、Sequence ID単位でF7上書きを優先します。保存前検証、一時ファイルからの置換、バックアップ、外部変更との競合検出、破損時のBlender基準へのフォールバックを維持しています。**Use Blender base → Restore** は選択IDだけ解除し、他のID・敵配置・Player Visualのドラフトを保持します。

既存Blender Exporterは生成物以外のファイルをそのままコピーするため、Bezier／ハンドル情報を含む専用ファイルも再Export後に保持されます。

## 変更ファイル

- `Game/CameraSequence.h`：互換JSON拡張、Bezier・接線・弧長・Easing評価、形状キャッシュ、de Casteljau分割。
- `Game/scene/Debug/EnemyPlacementEditorScene.Camera.cpp`：Drawトランザクション、挿入・ハンドル操作、評価器キャッシュ、履歴・プレビューとの連携。
- `Game/scene/Debug/EnemyPlacementEditorScene.Path.cpp`：新しい設定パネル、区間の投影選択、点／ハンドルの平面ドラッグ、経路・矢印・マーカー描画。既存シーンのメソッドを分割したファイルです。
- `Game/scene/Debug/EnemyPlacementEditorScene.cpp / .h`：Front／Side視点、モード・選択状態、描画中の未保存警告、既存操作との統合。
- `CG2_Setup.vcxproj / .filters`：追加ソース登録。
- `tests/CameraPathTests.cpp`、`tests/CameraPathEditorTests.h`：数値評価・実描画上の入力テスト。
- `tests/EnemyPlacementEditorTests.h`、`Tools/test-camera-path.ps1`、`Tools/test-enemy-placement.ps1`：既存テストへの統合・結果保存。
- `Tools/blender/test_player_visual_export.py`：Bezier情報を含む専用ファイルの再Export保持テスト。

CameraSequenceOverridesとStageLoaderは既存の実装を利用し、この拡張のための別保存処理や別カメラシステムは追加していません。

## 検証結果

- Debug／Releaseビルド：警告0、エラー0。
- `Tools/test-camera-path.ps1`：Linear／Bezier混在、複数位置でのde Casteljau分割前後の一致、周囲の経路の保持、PlayerEyeと合計時間の保持、Autoからの分割、弧長に基づく速度、キャッシュ再利用・更新、Easingと時刻の逆変換、FollowTangentとゼロ接線、JSON往復・不正値の拒否。
- `Tools/test-enemy-placement.ps1 -Configuration Debug / Release`：F7実画面へマウス・キーイベントを投入。Top描画、Enter／Esc／Backspace、点とハンドルのドラッグ、ドラッグ1回のUndo／Redo、線選択、Linear挿入、Bezierダブルクリック挿入と形状一致、Auto／Aligned／Free、Front／Sideの固定平面移動、Add from View、編集後プレビューとStop、独立保存・再読込・Blender復帰、ゲーム側との一致を検証。従来の敵配置編集・テストプレイ・SRV再利用も回帰確認。
- `Tools/test-camera-sequence.ps1`：従来のStage01〜03の補間、FPS引き継ぎ、Blender参照サンプルとの一致などが継続して通過。
- `Tools/test-camera-overrides.ps1`：IDごとの優先適用、旧データの読込、保存失敗・競合・バックアップ・解除・不正ファイルのフォールバックが通過。
- `Tools/test-camera-runtime.ps1`：Stage01〜03の通常開始演出をDebug／Releaseで回帰確認。
- `Tools/test-blender-player-visual.ps1`：Blenderの実ExporterでBezier／ハンドル入り専用ファイルのバイト保持を確認。
- 作業開始時の28個のステージJSON／blend／glTF／バイナリとハッシュ比較し、元データが変わっていないことを確認。

実描画画像は `generated/placement-tests/Debug/` と `Release/` の `camera-path-top.png`、`camera-path-front.png`、`camera-path-side.png`、`camera-path-preview.png`、結果は `camera-path-result-Debug.txt / Release.txt` に保存します。

未検証：物理マウス／キーボードによる操作感、異なるDPI・解像度、1024点での操作性能。通常開始演出のSpace入力分岐は非フォーカスのテストウィンドウでは未検証です（Skip評価は検証済み）。自由な3D Paint Pathと開始演出以外のイベント接続は今回の対象外です。
