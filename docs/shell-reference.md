# Shell 命令参考

每条命令以换行结束，成功输出以 `OK` 开头，失败输出以 `ERR` 开头。Shell 单行最大长度为
255 字节。命令不区分首尾空白，但参数之间使用空格分隔。

## 状态和站点

| 命令 | 成功输出 | 说明 |
|---|---|---|
| `status` | `OK status generation=N staged=M applied=K` | 显示代次、暂存架次和已应用架次 |
| `site set latitude LAT longitude LON` | `OK site staged` | 设置暂存配置中心点；纬度 -90..90，经度 -180..180 |
| `help` | `OK help site|group|config|fleet|status` | 显示入口分类 |

## 机群

两种添加形式都支持：

```text
group add NAME COUNT PROTOCOL TRANSPORT
group add NAME count COUNT protocol PROTOCOL transport TRANSPORT
```

`PROTOCOL` 为 `gb` 或 `odid`；`TRANSPORT` 为 `ble4`、`ble5`、`wifi24`、`wifi5` 或
`wifi58`。例如：

```text
group add fleet count 10 protocol odid transport ble5
group add gb_test 2 gb wifi24
```

| 命令 | 成功输出 |
|---|---|
| `group list` | `OK groups=N NAME:COUNT ...` |
| `group clear` | `OK group_clear` |
| `group delete NAME` | `OK group_delete` |
| `fleet list` | `OK fleet=N ID ...` |

`config group` 前缀也兼容上述 `group` 命令，例如 `config group list`。

## 应用和持久化

| 命令 | 成功输出 | 失败示例 |
|---|---|---|
| `config check` / `config validate` | `OK aircraft=N` | `ERR INVALID_CONFIG` |
| `config apply` | `OK generation=N running=M` | `ERR OPERATION_FAILED` |
| `config save` | `OK saved generation=N` | `ERR NO_APPLIED_CONFIG` |
| `config rollback` | `OK rollback running=M` | `ERR NO_APPLIED_CONFIG` |

`apply` 只替换成功校验并成功启动的运行快照；`save` 只保存最后一次成功应用的快照。
未应用的暂存修改不会写入 NVS。组名重复、总架次为 0 或超过 50、协议与承载组合不支持时，
`check` 和 `apply` 都会返回 `ERR INVALID_CONFIG`。

当前版本尚未提供 `group set`、消息周期 Shell 覆盖、`radio status`、`stats show`、
`system stop/start` 命令；这些不能写入验收脚本作为已实现接口。
