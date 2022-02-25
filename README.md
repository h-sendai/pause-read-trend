## ソケットを使ってデータを読むプログラム (ときどきEthernet Pause frameを送る)

1秒に1度、何バイト読んだかを表示する。
バイトの単位はMiB。

pause frameを送る間隔は-Iで指定する（単位: 秒。デフォルト5秒）

例:

```
./pause-read-trend リモートホスト:ポート 使用するNIC名 送るポーズタイム(1 - 65535)
./pause-read-trend remote:1234 exp0 64k
```

ポーズタイムは32kのように指定すると32768を指定したことになる。
64kと指定すると特別に65535を指定したことになるようにしてある。
