# モデル読み込みの高速化

## 変更内容

通常のModel／Object3d／TextureManagerの読み込み経路に適用する。元アセット、Skinningウェイト、アニメーション、Cloth Solverは変更していない。

- ボーン表示用のマーカー・接続線Object3dは、表示を有効にしてUpdateするときに初めて作る。モデル変更時には破棄し、選択色やマーカーMeshの変更にも対応する。
- テクスチャはモデル単位のスコープでまとめて転送する。専用のDirectコマンドリストを使い、未提出の描画コマンドをリセットしない。転送用メモリを保持してFence後に解放する。128MiB以上では途中提出して一時メモリを抑えるため、大きいモデルは複数回の提出になる。
- CPUモデルデータをバージョン・チェックサム付きのバイナリに保存する。Mesh／Material／Node／ウェイト／Animation／埋め込み画像の元データを保持する。起動後のSkeleton・GPUリソースはその都度生成する。
- 暗黙の既定アニメーション名も保存する。unordered_mapの復元順序が変わっても、通常インポートと同じClipを選ぶ。
- 外部画像・埋め込み画像のデコード済みMipMapを、DDSデータとチェックサムを含む`.ytex`として保存する。色空間やピクセルを保ち、非可逆圧縮は追加しない。既存DDSの圧縮やMipMapは再生成しない。
- 相対・絶対・`resources/`付きパスの別表記によるModel／Textureの重複生成を減らす。埋め込み画像のキーはモデルファイルごとに分ける。
- `ModelManager::PreloadModel`は最大2件のCPU準備をバックグラウンドで進める。Model解析、画像のデコード、MipMap生成、キャッシュ作成までを行い、GPU／SRV／共有TextureManagerへの登録はメインスレッドの`LoadModel`で行う。既存`LoadModel`は同期APIとして使える。
- Cloth Showroomは選択モデルだけを先に作り、もう一方をCPUプリロードする。準備中に選択を変えても現在のプレビューを動かし続け、準備完了後に切り替える。別のShowroom／ゲームシーンは既存の同期ロードを維持し、共通のキャッシュ・遅延ボーン生成・転送のまとめ処理が効く。

## キャッシュと事前生成

出力先は`generated/asset-cache/`。削除しても元ファイルから再生成する。Model／Texture本体は同じ起動中に再利用するため、読み込み済みアセットのファイル変更は再起動後に反映する。

モデルは元ファイルのサイズ・更新時刻とキャッシュ形式のバージョンを検査する。OBJの参照MTL、glTFの外部Buffer、AnbyのGLBが補完に使うPMXも検査する。外部画像は画像自身のサイズ・更新時刻、埋め込み画像は元画像バイト列のハッシュで判別する。欠損・破損・古い形式・書き込み失敗では通常のインポート／デコードへ戻る。読み込み処理の意味を変えた場合は`ModelCache::version`を更新する。

初回はモデル解析・MipMap生成に加えてキャッシュを書き込むため、必ずしも短くならない。開発中に先に生成する場合はReleaseビルド後に以下を実行する。ウィンドウやGPUデバイスを作らず、データをCPUで準備する。

```powershell
./tools/prepare-assets.ps1
# 他のモデルも指定可能（resourcesを基準にしたパス）
./tools/prepare-assets.ps1 -Models @('CGTest/RobotExpressive.glb', 'gltf/test.gltf')
```

生成結果は`generated/loading/prepare-result.txt`、工程別計測は`prepare-stages.csv`に保存する。元モデルに参照だけがあり実ファイルがない画像は、従来の白テクスチャへのフォールバックを維持してスキップし、`missing-textures.txt`へ記録する。今回の2モデルでは11画像を事前生成し、欠けている`MyGtYUhe6t/sph/B.jpg`の1件を記録した。キャッシュはローカルの絶対パスを含むため、別の配置先へコピーしたときには、その配置先で再生成する。

## 計測と検証

`YAN_LOAD_PROFILE=1`で工程別の時間を記録し、終了時に`generated/loading/session.csv`へ出力する。Model解析、キャッシュ読込／保存、Skeleton、GPUバッファ・Material、画像デコード、MipMap、転送記録、GPU提出・待機、SkinCluster、デバッグ用ボーン生成を区別する。親工程は子工程を含むので、CSVの全行を足すと総時間にはならない。通常は記録しない。

```powershell
./tools/build.ps1 -Configuration Debug
./tools/test-asset-loading.ps1
./tools/build.ps1 -Configuration Release
./tools/test-asset-loading.ps1 -Executable ../generated/outputs/Release/CG2_Setup.exe
./tools/test-cloth-runtime.ps1
./tools/test-cloth-solver.ps1
./tools/test-showroom.ps1
./tools/test-title-scene.ps1
```

比較テストは新規のキャッシュnamespaceを作り、同じ実行ファイルで3つの条件を順番に起動する。元アセットや既存キャッシュを削除しない。

1. `baseline`：ディスクキャッシュと転送まとめ処理を無効にし、ボーン表示用Object3dを従来どおり先に作る。
2. `cold`：通常の高速化を有効にして、空のキャッシュから読み込む。
3. `warm`：別プロセスから同じキャッシュを使う。

起動中のメモリキャッシュによる速さとは分け、各モデルの`Object3d::SetModel`の時間を測る。ウィンドウ・シェーダー・ゲーム全体の起動時間を含む値ではない。また過去コミットの実行ファイルとの比較ではなく、現実装の機能切り替えによる比較である。初期起動、OSファイルキャッシュ、GPUドライバー等によって値は変動する。

結果は`generated/loading-tests/comparison.csv`、各条件の工程は`*-stages.csv`、データ検証は`*-models.csv`に保存する。3条件で、Geometry、ウェイト、Material、Node、Animationを含むモデルデータのバイナリダイジェストが一致することを確認する。別に、DDSピクセル・色空間・MipMap、破損時の再生成、元ファイル／MTL／外部Bufferの更新検出、書き込めないキャッシュ、パス別名、遅延ボーン表示、埋め込み画像のプリロードを検証する。

同梱AssimpはFBX非対応（`FBX=0`、`GLB/GLTF/OBJ=1`）だったため、検証対象はPMX・GLB・glTF・OBJ。FBXローダーの追加やAssimpの更新は今回行っていない。

最終実装のDebug計測（秒、単発測定）：

| モデル | 旧処理相当 | キャッシュ初回 | キャッシュ再利用 |
| --- | ---: | ---: | ---: |
| MyGtYUhe6t | 6.665 | 6.376 | 0.819 |
| ema | 40.844 | 43.011 | 5.617 |
| RobotExpressive（GLB、14 Clip） | 0.397 | 0.125 | 0.019 |
| gltf/test.gltf（1 Clip） | 1.693 | 1.601 | 0.290 |

GPU提出・待機回数はAnbyで4→1、emaで7→4。emaは転送メモリの上限によって分割される。非表示のボーンObject3dはAnbyで467→0、emaで753→0。再利用時はAnbyで約88%、emaで約86%短縮した。キャッシュ初回のemaは保存コストで少し長くなっている。

最終実装のRelease計測（秒、単発測定）：

| モデル | 旧処理相当 | キャッシュ初回 | キャッシュ再利用 |
| --- | ---: | ---: | ---: |
| MyGtYUhe6t | 2.445 | 1.536 | 0.366 |
| ema | 10.920 | 11.009 | 2.912 |
| RobotExpressive（GLB、14 Clip） | 0.314 | 0.040 | 0.007 |
| gltf/test.gltf（1 Clip） | 0.625 | 0.397 | 0.101 |

Debug／Releaseともビルドと3条件の読み込みテストを通過し、既定Clipを含むデータの一致を確認した。Cloth Solver、通常Showroom、ClothのGPU描画、タイトル画面のGPU描画テストも通過した。Clothでは2モデルのIdle／Walk／Run、非同期切り替え、Skinning、Collider追従、Physics切り替えを確認した。キャプチャ画像でMyGtYUhe6tの斜め視点とemaのデバッグ表示も確認した。

Debug／Releaseの比較結果はそれぞれ`generated/loading-tests/Debug/`、`Release/`にも保存する。通常起動で初回生成を避けたい場合は、上記のReleaseによる事前生成ツールを使う。

## 今後の候補

表示中のCPU負荷への追加修正（Skinning PaletteのUpload Heap読み戻し削除、Clothの反復処理の短縮）は`docs/cloth-physics-prototype.md`の「emaのドロワと表示中の負荷の追加修正」に記載する。こちらの表は読み込み処理の計測であり、フレーム更新時間とは別である。

キャッシュ再利用後も画像の読み込み・チェックサム検証・GPUメモリ確保・転送は必要になる。テクスチャが大きいemaではこの時間が残る。さらに短くする場合は、画質を確認しながら画像解像度やBC圧縮を調整する、共有Skinning用の不変データをまとめる、他シーンでもCPUプリロードAPIを使う、という順で計測する。GPU確保・提出は今もメインスレッドにあり、完全に無停止のストリーミング機構ではない。
