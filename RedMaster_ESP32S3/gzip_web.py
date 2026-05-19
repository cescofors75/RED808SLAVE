import gzip, os

web_dir = 'data/web'
for f in os.listdir(web_dir):
    if f.endswith(('.js', '.css', '.html')) and not f.endswith('.gz'):
        src = os.path.join(web_dir, f)
        dst = src + '.gz'
        with open(src, 'rb') as fin:
            data = fin.read()
        with open(dst, 'wb') as fout:
            fout.write(gzip.compress(data, 9))
        orig = os.path.getsize(src)
        comp = os.path.getsize(dst)
        ratio = round(comp / orig * 100, 1)
        print(f'{f}: {orig//1024}KB -> {comp//1024}KB ({ratio}%)')
