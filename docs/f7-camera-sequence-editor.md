# F7 Camera Sequence エディター

線・Bezier・ハンドル・Front／Side編集の追加機能は [F7カメラパス編集](f7-camera-path-editor.md) を参照してください。

既存の `EnemyPlacementEditorScene` の左設定パネル／右3Dビューポートを拡張しています。別のシーンや補間システムは作成していません。ゲーム開始時と同じ `CameraSequence::Evaluate()` をプレビューと経路描画に使用します。

## 操作

1. F7でステージエディターを開き、左上でステージを選択します。
2. **Camera Sequence** を選び、**Sequence ID** で `start` を選択します。
3. **Points** 一覧、または3D画面の丸いマーカーをクリックしてポイントを選択します。黄色が選択中、緑が最終FPS位置、水色がその他のポイントです。
4. **Move gizmo** のXYZハンドルをドラッグして移動します。**Rotate gizmo** では回転軸X/Y/Zを選び、画面のリングをドラッグします。回転ギズモの操作はRotation modeをManualへ切り替えます。
5. 左パネルをスクロールすると、選択ポイントのID・位置・回転・Duration・Interpolation・Rotation mode・Look At Target・Look At Player offset・FOV・PlayerEyeを編集できます。回転入力は度数法、保存時のrotationは既存仕様どおりラジアンです。数値はドラッグ、Ctrl+クリックによる直接入力に対応します。
6. **Add from view** は編集視点から追加、**Duplicate** は選択ポイントの複製、**Delete** は削除、**Move up / down** は順番変更です。追加時は終端ポイントの前に挿入します。最低2点とPlayerEyeの終端制約を維持します。
7. **Play / Pause / Resume / Restart** と **Time (s)** で演出を確認します。シークすると一時停止します。**Final FPS / PlayerEye** で最終FPS視点を確認できます。再生終端では一時停止します。
8. **Stop** でプレビュー前の編集視点・Camera Mode・FOV・透視／正射影・near/far・表示フィルターを復元します。再生中はポイントやステージの編集をロックし、AI・物理・ゲーム進行は開始しません。
9. **Save camera only** で選択中のSequence IDだけ保存します。**Reload cameras** で保存済みの全カメラ経路を読み直します。未保存変更には確認を表示します。
10. **Use Blender base → Restore** で選択IDのオーバーライドを解除します。Blenderに存在しないF7専用IDの場合、そのIDを削除します。他のID・敵配置・Player Visualの編集中データは保持します。

**Free / FPS / Top** と既存のTab、WASD、Q/E、Shift、Alt操作を利用できます。Topではホイールでズーム、中ボタンドラッグで平行移動します。**Focus point (F)** は選択ポイントへ編集視点を移します。FPSはエディター内の移動用プレイヤーです。実際の開始位置やゲーム側FPSカメラ制御を変更しません。

**Show path** は曲線・ポイント・方向矢印・ギズモをまとめて表示／非表示にします。曲線は実時間の補間評価結果をサンプリングして描きます。**Stage visibility** は編集時の屋根・天井・壁・上下階の描画設定です。演出プレビューは完全なステージを描画し、Stopで編集時のフィルターに戻ります。

**Player model** は既存の `StartPlayerVisual` を表示／非表示にします。以前の **Player Visual** のYaw設定をそのまま使用します。YawはEnemy PlacementモードのPlayer Visualセクションで調整・個別保存できます。Normalモデルの補正値は既存の+90度です。

**Undo / Redo** またはCtrl+Z/Yに対応します。直近100状態まで、カメラのドラフトを対象とします。ファイルへの保存・Blenderへの復帰自体はUndoしません。Undo後に必要な経路を再保存してください。

**Create another sequence** を開くと別IDを作成できます。ゲームのイベント接続は今回追加しておらず、通常開始時に再生するIDは従来どおり `start` です。

**Test play (placements)** は既存の敵配置テストプレイです。未保存の敵配置を使用し、F8で編集に戻ります。カメラだけの確認にはPlayを使用します。

## 保存形式とBlenderとの関係

ステージJSON内の `cameraSequences` はBlender側の基準データとして保持します。F7は同じディレクトリの専用ファイルへ保存します。

```
resources/levels/stage03/stage03.json                  Blenderのステージ
resources/levels/stage03/stage03.camera_sequences.json F7のカメラ上書き
resources/levels/stage03/enemy_placements.json         既存の敵配置
```

例（既存CameraSequenceと同じポイント仕様）：

```json
{
  "version": 1,
  "stage": "stage03",
  "cameraSequences": [
    {
      "id": "start",
      "enabled": true,
      "points": [
        {
          "id": "CameraPoint_1", "order": 0,
          "position": [32, 14, 20], "rotation": [0, 0, 0],
          "duration": 1.5, "interpolation": "SmoothStep",
          "rotationMode": "LookAtPlayer", "lookAtOffset": [0, 1.6, 0],
          "fov": 60, "playerEye": false
        },
        {
          "id": "PlayerEye", "order": 1,
          "duration": 0, "interpolation": "Linear",
          "rotationMode": "Manual", "fov": 60, "playerEye": true
        }
      ]
    }
  ]
}
```

`StageLoader` はSequence ID単位でオーバーライドを優先し、記載されていないIDはBlenderの基準を使います。ファイルがない従来ステージも互換です。`enabled:false` の上書きにも対応します。壊れたファイル・stage/version違い・合計128 ID超過は警告を残してBlender側にフォールバックします。エディターはその状態での上書き保存を停止します。

保存前は既存のパーサーで検証します。2〜1024点、ID/orderの一意性、数値範囲、終端duration=0、途中duration>=0.001秒、PlayerEyeは終端のみ、startはPlayerEye終端、Holdの次点は同一姿勢など、従来の条件を変更していません。不正なドラフトは編集を継続できますが、保存／再生は検証に通るまで停止します。

書き込みは同じディレクトリの一時ファイルを再検証してから置換し、既存ファイルは `.camera-backup` に保持します。外部変更を検出した場合も保存を中止します。最終オーバーライドを解除すると `cameraSequences: []` のファイルを残し、全IDがBlender側のデータに戻ります。

既存Blender Exporterは出力ディレクトリを作業領域へコピーしてから生成物だけを差し替えるため、専用ファイルをバイト単位で保持します。Blenderの経路を再Exportすると基準データは更新され、F7で上書きしているIDは維持されます。Use Blender baseで最新のBlender経路へ戻せます。

## 主な変更ファイル

- `Game/scene/Debug/EnemyPlacementEditorScene.cpp / .h`：既存シーンへのモード追加、入力・未保存警告・描画連携。
- `Game/scene/Debug/EnemyPlacementEditorScene.Camera.cpp`：ポイント操作、3D選択／ギズモ、曲線、共有評価器のプレビュー、履歴、保存／復帰UI。
- `Game/CameraSequence.h`：既存スキーマのJSON出力と解決済みポイント姿勢の取得を追加。補間・評価式は維持。
- `Game/CameraSequenceOverrides.h`：カメラ専用ファイルの読み書き・検証・競合検出・IDごとの結合。
- `Game/StageLoader.h`：Blender基準とF7上書きの読み込み。
- `Game/scene/Main/GameScene.cpp`：オーバーライドの警告出力。開始演出は既存の読み込み済み経路を利用。
- `Game/player/Player.h`：プレビュー用にSettingsのconst参照アクセサー追加。
- `CG2_Setup.vcxproj / .filters`、`.gitignore`：追加ファイル登録と一時／バックアップファイル除外。
- `tools/blender/yanengine_level_exporter.py`：既存コピー処理がカメラ上書きも保持することを明記。

## 検証

- Debug／Release：警告0・エラー0でビルド。
- `tools/test-camera-overrides.ps1`：Stage01〜03、JSON往復、ID単位優先、新IDと無効化、独立保存、バックアップ、検証失敗／競合時の保持、再Export相当の基準変更、解除、不正ファイルのフォールバック。
- `tools/test-enemy-placement.ps1 -Configuration Debug / Release`：実際のF7描画上にImGuiマウス入力を投入し、XYZ移動・回転リング・マーカー選択を確認。追加／削除／複製／順序変更、Undo/Redo、再生／一時停止／再開／シーク／Restart／最終FPS、Stop後の投影行列と姿勢の完全一致、表示フィルター復元、独立保存／再読み込み／解除、通常GameSceneへの保存経路適用、既存敵配置テストと8回のリソース再利用を検証。
- `tools/test-camera-runtime.ps1`：Stage01〜03の通常開始演出、FadeIn、ゲーム進行停止、FPS引き継ぎ、Skip／Restartなどを確認。
- `tools/test-camera-sequence.ps1`、`tools/test-stage-loader.ps1`：既存補間とステージ読込の回帰確認。
- `tools/test-blender-player-visual.ps1`：Blender 5.2の実Exporterで複数回再Exportし、変更済みカメラ専用ファイル・敵配置のバイト一致、Blender基準の更新、既存Yaw保持、元blend非変更を検証。

実描画画像は `generated/placement-tests/Debug/` と `Release/` の `camera-editor-route.png`、`camera-editor-preview.png` に保存します。専用テスト結果は `camera-editor-result-Debug.txt / Release.txt`、その他の結果は同ディレクトリの `result-*.txt` と `generated/camera-tests/` を参照してください。

未検証：実機の物理マウス／キーボードでの操作感、異なるDPI・解像度、1024ポイントでの操作性能。通常開始演出のSpaceキー入力分岐は、非フォーカスのテスト用ウィンドウのため未検証です（評価器のSkipは検証済み）。3Dの選択・ドラッグ自体は実描画上への入力投入で検証しています。既存の未コミット変更を保持し、元のステージJSON・blend・敵配置ファイルへテスト内容を書き込んでいません。
