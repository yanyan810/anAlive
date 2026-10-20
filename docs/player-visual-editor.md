# F7 Player Visual のYaw調整

既存のEnemyPlacementEditorSceneを拡張した。開始演出とF7プレビューは同じ`StartPlayerVisual`を使用する。

Normalモデルの元の正面はローカル **−X**、FPSの正面は **＋Z**。そのままPlayerのYawを渡すと約90度横を向くため、Stage01 / Stage02 / Stage03には見た目の補正 **＋90度** を設定した。Player本体、FPSカメラ、CameraSequenceとカメラ経路には適用しない。

## 操作

1. F7で既存の配置エディターを開き、左のステージ選択で対象を選ぶ。
2. 上部の **Player Visual** を開く。
3. **仮プレイヤーモデルを表示** をオンにし、必要なら **Player Visualへカメラ移動** を押す。モデルはPlayer Spawnの位置・Yawを基準に表示する。FPSモードで編集カメラを動かしてもモデルが付いてくることはない。
4. **Yaw offset** スライダー、または **Yaw (degrees)** 数値入力で −180～180度を指定する。次の描画フレームに反映する。Normalでは＋90度が補正値。
5. **プレイヤーとモデルの正面矢印** をオンにする。緑がFPSのSpawn正面、オレンジが補正後のモデル正面。2本が一致すれば向きが合っている。重なったときも確認できるよう高さを少し変えてある。
6. **Player Visualのみ保存** を押す。下の敵配置用「保存」とは独立した処理。
7. **Player Visualを再読込（編集破棄）** でステージJSONの保存値を読み直す。この操作は敵配置の編集中データを変更しない。

「読込値へ戻す」は直前の読み込み・保存時のYawへ戻す。モデル表示と矢印の表示切替はプレビュー専用で、ゲーム側のモデル表示条件には影響しない。未保存のPlayer Visualは、敵配置と同様にステージ切替・退出時の破棄確認対象になる。

## 保存とBlender再Export

各ステージのJSONルートに保存する:

```json
"playerVisual": {
  "yawOffsetDegrees": 90
}
```

F7の保存はこのオブジェクト内の`yawOffsetDegrees`だけを更新する。オブジェクトの未知のプロパティは将来用として維持する。既存の敵・Spawn・カメラ経路などは、値だけでなく数値表記や空白も変更しない。

同じディレクトリへ一時ファイルを書き、検証後に置き換える。元ファイルは`.player-visual-backup`へ保存する。外部でステージJSONが変更されていた場合は保存を止めて再読み込みを求める。敵配置側の`enemy_placements.json`とそのバックアップには触れない。

Blenderの通常ExportはステージJSON全体を作り直すため、アドオン **1.13.1** ではExport先にある既存ステージJSONから`playerVisual`だけを引き継ぐようにした。レイアウト・カメラポイントは引き続きBlenderが生成し、敵配置のsidecarは既存どおりコピーで維持する。再Export後もYaw設定と将来のモデル設定フィールドが残ることを実際のExportで検証した。

**更新した`tools/blender/yanengine_level_exporter.py`を再インストールしてから、同じステージ出力先へExportすること。** 古いアドオンではこの引き継ぎ処理がない。新しい空の出力先には元のF7設定がないため、必要ならステージJSONも一緒に移す。`.blend`へF7のYawを重複保存しないので、古い.blendを開いてもF7の新しい保存値を上書きしない。

不正なYawや壊れたPlayer Visual設定がある場合、BlenderのExportは上書きせず失敗する。ゲーム側は警告と0度へのフォールバック、F7側は専用保存を停止する。設定なしの旧ステージは0度として互換動作する。

## 実装箇所

- `Game/PlayerVisualSettings.h`: 度数法の設定・検証、ステージJSONの対象部分だけを更新する保存処理。
- `Game/StageLoader.h`: `playerVisual`読み込み。
- `Game/StartPlayerVisual.h`: `playerYaw + offset`をモデルだけへ適用。モデル本来の前方もこの描画クラスで定義する。
- `Game/scene/Main/GameScene.cpp`: 開始演出でロード済み設定を描画へ渡す。
- `Game/scene/Debug/EnemyPlacementEditorScene.{h,cpp}`: UI、同じ描画用モデル、矢印、独立した編集中の値と保存・再読込。
- `tools/blender/yanengine_level_exporter.py`: F7所有フィールドの再Export時の保持。

正式モデルへの差し替えは`StartPlayerVisual`のモデルバインドで行う。新モデルの元の正面が異なる場合は同クラスの`LocalForward()`を合わせる。Yawの合成・F7の編集・JSON保存・ゲームの読み込みはそのまま利用できる。EnemyやPlayerの本体Yawへ補正を加える必要はない。

## 検証結果

- Debug / Releaseのビルド成功（警告0、エラー0）。
- 設定の型・範囲・旧JSON、部分保存・バックアップ・外部変更防止、保存後のStageLoader再読込がPASS。
- Debug / Releaseの実F7描画で0度と90度の画像差、表示切替、Normalの90度補正後の前方一致を検証。
- F7に未保存の敵配置編集を残したまま、別のステージJSONコピーへ45度を保存→別の角度へ変更→専用再読込で45度が復元されることを検証。敵配置データ・dirty状態は維持され、元のステージJSONと敵sidecarにはテスト中の書き込みなし。
- 正式な保存経路と同じ関数を使った上記コピー検証後、元のステージの90度を読み直したモデル画像が保存前の画像と完全一致。
- Player本体・FPSの向き・カメラ経路が変わらないこと、既存の敵配置テストプレイと8回の再読込・再テストでSRV数が安定することを検証。
- 開始演出の実D3D12テストでも、Stage03のDebug / Releaseで保存済みYawがモデルへ反映され、FPS復帰まで正常であることを確認。
- Blenderの実Exportを繰り返し、Yawと将来用フィールド、敵sidecarを保持し、不正設定時は既存ファイル群を変更しないことを検証。

テスト:

```powershell
./tools/test-player-visual.ps1
./tools/test-blender-player-visual.ps1
./tools/test-enemy-placement.ps1 -Configuration Debug
./tools/test-enemy-placement.ps1 -Configuration Release
```

画像は`generated/placement-tests/Debug/`と`Release/`の`player-visual-editor-yaw-0.png`、`player-visual-editor-yaw-90.png`、`player-visual-editor.png`など。物理マウス・キーボードでの手動操作確認は未実施。モデル描画・F7オペレーション・保存・再読み込みは実行テストで確認した。
