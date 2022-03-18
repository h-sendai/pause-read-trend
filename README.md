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

## Intel XL710 for 40GbE QSFP+ ドライバ (i40e)

Linux用 Intel XL710 for 40GbE QSFP+ ドライバ (i40e)には
ユーザープロセスがpause frameを送れないようにするコードが
入っている。目的は
[CVE-2015-1142857](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-2015-1142857)
に対応するためのようだ。
mainline kernelでは
https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=e7358f54a3954df16d4f87e3cad35063f1c17de5
のコミットで入れられている。

この修正を取りのぞくとi40eでもユーザープロセスからpause frameを
送ることができるようになる。以下CentOS Stream 8での作業方法。

参考: https://wiki.centos.org/HowTos/BuildingKernelModules

### rpmファイル展開場所の確保

- rpmdevtoolsパッケージをインストールする。``dnf install rpmdevtools``
- ``$HOME/.rpmmacors``を作って``%_topdir``を指定する。これで指定したディレクトリ以下が
作業場所になる。
- $HOME/rpm以下での作業はroot権限は必要ない。

```
# $HOME/.rpmmacros
%_topdir /home/username/rpm
```
- ``rpmdev-setuptree``を実行

これで
/home/username/rpm以下に次のディレクトリができる
```
rpm
|-- BUILD
|-- BUILDROOT
|-- RPMS
|-- SOURCES
|-- SPECS
`-- SRPMS
```

### カーネルソースの取得

``rpm/SRPMS``に移動して
```
dnf download --source kernel
```
でkernel SRPMファイルが取得できる。

### カーネルソースの展開

```
rpm -ihv kernel-4.18.0-365.el8.src.rpm
cd ../SPECS
rpmbuild -bp kernel.spec
```

で``rpm/BUILD``以下にCentOS Stream 8カーネルソースが展開される。
``rpmbuild -bp kernel.spec``で依存物が足りないといわれたら
以下のコマンドで一括で依存物が入る(以下のコマンドはrootで実行する)。

```
root# dnf install dnf-plugins-core
root# dnf config-manager --enable powertools
root# dnf builddep kernel.spec
```

### configファイルのコピー、ドライバコンパイルの確認

```
cd rpm/BUILD/kernel-4.18.0-365.el8/linux-4.18.0-365.el8.x86_64
make oldconfig
make prepare
make modules_prepare
make M=drivers/net/ethernet/intel/i40e
strip --strip-debug drivers/net/ethernet/intel/i40e
```

(注)
``make oldconfig``でプロンプトがでて入力を求められるときは
稼働中のカーネルバージョンと、展開したカーネルソースのバージョンが
一致していないので確認する。たとえば``uname -r``で稼働中のカーネル
バージョンが取得できる。
(注終わり)

(注)``configs/``ディレクトリに各アーキテクチャのconfigファイルがあるが
これを``cp configs/... .config``とする必要はない。というかコピーすると
ロード時に

```
[   16.110529] ixgbe: Unknown symbol lockdep_rcu_suspicious (err 0)
[   16.177005] ixgbe: Unknown symbol __asan_report_load1_noabort (err 0)
[   16.228153] ixgbe: Unknown symbol debug_dma_mapping_error (err 0)
:
```
となるドライバができてしまう。(注終わり）

``make M=drivers/net/ethernet/intel/i40e``の出力は次のようになる:

```
localhost% make M=drivers/net/ethernet/intel/i40e

  WARNING: Symbol version dump ./Module.symvers
           is missing; modules will have no dependencies and modversions.

  CC [M]  drivers/net/ethernet/intel/i40e/i40e_main.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_ethtool.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_adminq.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_common.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_hmc.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_lan_hmc.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_nvm.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_debugfs.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_diag.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_txrx.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_ptp.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_ddp.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_client.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_virtchnl_pf.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_xsk.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_dcb.o
  CC [M]  drivers/net/ethernet/intel/i40e/i40e_dcb_nl.o
  LD [M]  drivers/net/ethernet/intel/i40e/i40e.o
  Building modules, stage 2.
  MODPOST 1 modules
  CC      drivers/net/ethernet/intel/i40e/i40e.mod.o
  LD [M]  drivers/net/ethernet/intel/i40e/i40e.ko
localhost%
```

これでドライバファイルdrivers/net/ethernet/intel/i40e/i40e.ko
ができればコンパイル環境はOKである。

###

i40e_main.c内に
```
i40e_add_filter_to_drop_tx_flow_control_frames()
```
関数を呼んでいるところが2箇所あるのでこの呼び出しをコメントアウトする。
``#if 0 ... #endif``を使うなら以下のとおり：

```
--- i40e_main.c.orig    2022-02-05 02:48:52.000000000 +0900
+++ i40e_main.c 2022-03-12 13:35:58.406318565 +0900
@@ -10761,6 +10761,7 @@
        if (pf->flags & I40E_FLAG_MSIX_ENABLED)
                ret = i40e_setup_misc_vector(pf);

+#if 0
        /* Add a filter to drop all Flow control frames from any VSI from being
         * transmitted. By doing so we stop a malicious VF from sending out
         * PAUSE or PFC frames and potentially controlling traffic for other
@@ -10769,6 +10770,7 @@
         */
        i40e_add_filter_to_drop_tx_flow_control_frames(&pf->hw,
                                                       pf->main_vsi_seid);
+#endif

        /* restart the VSIs that were rebuilt and running before the reset */
        i40e_pf_unquiesce_all_vsi(pf);
@@ -15821,6 +15823,7 @@
                dev_warn(&pdev->dev, "MFS for port %x has been set below the default: %x\n",
                         i, val);

+#if 0
        /* Add a filter to drop all Flow control frames from any VSI from being
         * transmitted. By doing so we stop a malicious VF from sending out
         * PAUSE or PFC frames and potentially controlling traffic for other
@@ -15829,6 +15832,7 @@
         */
        i40e_add_filter_to_drop_tx_flow_control_frames(&pf->hw,
                                                       pf->main_vsi_seid);
+#endif

        if ((pf->hw.device_id == I40E_DEV_ID_10G_BASE_T) ||
                (pf->hw.device_id == I40E_DEV_ID_10G_BASE_T4))
```

修正後、再コンパイル。

```
cd rpm/BUILD/kernel-4.18.0-365.el8/linux-4.18.0-365.el8.x86_64
make M=drivers/net/ethernet/intel/i40e
strip --strip-debug drivers/net/ethernet/intel/i40e
```

### インストール

``/lib/modules/$(uname -r)/updates/drivers/net/ethernet/intel/i40e/``ディレクトリ
を作成し、ここに``i40e.ko``をコピーする。

### initramfsの生成

稼働中のドライバは更新できないように思うのでinitramfs
を作りなおしてリブートすることでコンパイルしたドライバを使う。

```
root# depmod -a
root# dracut test.img
```

でinitramfsファイル (test.img)ができる。
できたinitramfsは

- Early CPIO image (CPUのマイクロコード )
- ドライバなどがはいったファイルツリー

の部分に分かれているので単にgzipで伸長しcpioで取り出しという
ことができない。
中のファイル一覧を見るには

```
/usr/lib/dracut/skipcpio test.img | gzip -cd | cpio -itv
```

とする。

i40e.koはCentOS Stream 8付属のものと今回コンパイルしたもののふたつが
入っている。CentOS Stream 8のほうを消す必要はない。

### initramfsの入れ替え

よさそうなら``/boot``にあるinitramfsを入れ替えてリブートする。

### 起動後のdmesg

今回いれたi40e.koにはサインがないので起動後dmesgでみると
次のようになっている。

```
% dmesg | grep i40e
[    1.276728] i40e: no symbol version for module_layout
[    1.276732] i40e: loading out-of-tree module taints kernel.
[    1.276940] i40e: module verification failed: signature and/or required key missing - tainting kernel
[    1.291773] i40e: Intel(R) Ethernet Connection XL710 Network Driver
[    1.291774] i40e: Copyright (c) 2013 - 2019 Intel Corporation.
[    1.307085] i40e 0000:c1:00.0: fw 6.0.48442 api 1.7 nvm 6.01 0x800035da 1.1747.0 [8086:1583] [8086:0001]
[    1.377746] i40e 0000:c1:00.0: MAC address: 40:a6:b7:46:6f:58
[    1.378107] i40e 0000:c1:00.0: FW LLDP is enabled
[    1.492079] i40e 0000:c1:00.0 eth0: NIC Link is Up, 40 Gbps Full Duplex, Flow Control: None
[    1.523227] i40e 0000:c1:00.0: PCI-Express: Speed 8.0GT/s Width x8
[    1.545190] i40e 0000:c1:00.0: Features: PF-id[0] VFs: 64 VSIs: 66 QP: 32 RSS FD_ATR FD_SB NTUPLE DCB VxLAN Geneve PTP VEPA
[    1.557708] i40e 0000:c1:00.1: fw 6.0.48442 api 1.7 nvm 6.01 0x800035da 1.1747.0 [8086:1583] [8086:0001]
[    1.627783] i40e 0000:c1:00.1: MAC address: 40:a6:b7:46:6f:59
[    1.628138] i40e 0000:c1:00.1: FW LLDP is enabled
[    1.651618] i40e 0000:c1:00.1: PCI-Express: Speed 8.0GT/s Width x8
[    1.673630] i40e 0000:c1:00.1: Features: PF-id[1] VFs: 64 VSIs: 66 QP: 32 RSS FD_ATR FD_SB NTUPLE DCB VxLAN Geneve PTP VEPA
[    1.674262] i40e 0000:c1:00.0 enp193s0f0: renamed from eth0
[    1.689228] i40e 0000:c1:00.1 enp193s0f1: renamed from eth1
```
