* PMDのff(ffopm)形式の音色ファイルとgimic timble parameter(gtp)を相互変換します。
* Windowsの場合、エクスプローラ上で対象ファイルをドロップすると同じフォルダ内にファイルが出力されます。
* gtpファイルは、GIMICのパッチエディタ上の「Import...」機能で読み込みます。
* ffファイルに複数の音色ファイルが含まれている場合は、バンクファイル(patchbnk.000,patchbnk.001,...)を出力します。SDカード内の、/GMCAPPDT/(ワークスペース番号)/ に出力ファイルを配置してください。
```
Usage: ff2gtp <input> ..
<input> supports .ff(PMD), .gtp

<input>は複数指定可能です。
```
