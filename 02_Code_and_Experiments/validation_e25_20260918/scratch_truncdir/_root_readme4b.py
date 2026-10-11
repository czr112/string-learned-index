import io
p = "README.md"
t = io.open(p, encoding="utf-8", newline="").read()
old = "- **\u7b49\u6df1\u76ee\u5f55\u7ed3\u6784\u5019\u9009**\uff1a[\u6b63\u786e\u6027\u4e0e A/B \u7ed3\u679c](07_Eval_Integration/RESULTS_DEPTH_DIR_20261011.md)\u3002\r\n"
new = (old +
       "- **\u622a\u65ad\u5206\u9694\u7b26\u7b49\u6df1\u76ee\u5f55**\uff1a[\u6b63\u786e\u6027\u3001\u89c4\u6a21\u4e0e\u914d\u5bf9 A/B](07_Eval_Integration/RESULTS_DEPTH_DIR_TRUNC_20261011.md)\u3002\r\n")
assert t.count(old) == 1, t.count(old)
t = t.replace(old, new)

old2 = ("\u9ed8\u8ba4\u540e\u7aef\u4ecd\u4e3a Context16\u3002BlockLinearRank \u6b63\u786e\u6027\u901a\u8fc7\u4f46\u7ed3\u6784\u5165\u53e3\u56de\u5f52\uff1bEqualDepthDir \u5728 skew \u4e0a\u6709\u6536\u76ca\uff0c\r\n"
        "mixed \u4e3b\u76ee\u6807\u672a\u901a\u8fc7\uff0c\u4e24\u8005\u5747\u672a\u8bbe\u4e3a\u9ed8\u8ba4\u3002")
new2 = ("\u9ed8\u8ba4\u540e\u7aef\u4ecd\u4e3a Context16\u3002BlockLinearRank \u6b63\u786e\u6027\u901a\u8fc7\u4f46\u7ed3\u6784\u5165\u53e3\u56de\u5f52\uff1bEqualDepthDir \u5728 skew \u4e0a\u6709\u6536\u76ca\uff0c\r\n"
        "mixed \u4e3b\u76ee\u6807\u672a\u901a\u8fc7\uff1b\u5176\u622a\u65ad\u5f62\u6001\uff08EqualDepthDirTrunc\uff09\u8bc1\u5b9e\u4e86\u622a\u65ad\u673a\u5236\r\n"
        "\uff08mixed/skew \u66f4\u5feb\u3001\u76ee\u5f55\u5185\u5b58\u964d\u5230 1/9\u20131/2\uff09\uff0c\u4f46\u5bf9 C16 \u7684\u4e3b\u76ee\u6807\u4ecd\u672a\u901a\u8fc7\u3002\u4ee5\u4e0a\u5019\u9009\u5747\u672a\u8bbe\u4e3a\u9ed8\u8ba4\u3002")
assert t.count(old2) == 1, t.count(old2)
t = t.replace(old2, new2)
io.open(p, "w", encoding="utf-8", newline="").write(t)
print("root README updated")
