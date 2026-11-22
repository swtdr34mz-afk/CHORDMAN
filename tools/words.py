import pdfplumber, sys, json
pdf = pdfplumber.open(sys.argv[1])
out=[]
for pi in range(23, 33):
    p = pdf.pages[pi]
    ws = p.extract_words(keep_blank_chars=False, use_text_flow=False)
    for w in ws:
        out.append((pi, round(w['top'],1), round(w['x0'],1), w['text']))
json.dump(out, open('words.json','w'))
print(len(out))
