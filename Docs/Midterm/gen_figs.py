# 중간발표용 그림(SVG)과 원본 데이터(CSV)를 만든다. 외부 패키지 없음.
# 사용: python -I gen_figs.py <출력 폴더>
import csv, math, os, sys

OUT = sys.argv[1]
FIG = os.path.join(OUT, "figures"); DAT = os.path.join(OUT, "data")
os.makedirs(FIG, exist_ok=True); os.makedirs(DAT, exist_ok=True)

FONT = "Malgun Gothic, 'Apple SD Gothic Neo', sans-serif"
INK, MUTED, GRID, BG = "#1d2633", "#5f6b78", "#e3e7ec", "#ffffff"
C = ["#2f6fb0", "#d9822b", "#3a9d5d", "#8e5bb5", "#c2410c"]
W, H = 1600, 900

def wilson(k, n, z=1.96):
    p = k / n; d = 1 + z*z/n
    c = (p + z*z/(2*n)) / d; h = z*math.sqrt(p*(1-p)/n + z*z/(4*n*n)) / d
    return 100*max(0, c-h), 100*min(1, c+h)

def esc(s): return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

class Chart:
    def __init__(self, title, sub, xr, yr, xlabel, ylabel, L=150, R=230, T=150, B=120):
        self.s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{FONT}">',
                  f'<rect width="{W}" height="{H}" fill="{BG}"/>',
                  f'<text x="{L}" y="62" font-size="38" font-weight="700" fill="{INK}">{esc(title)}</text>',
                  f'<text x="{L}" y="104" font-size="24" fill="{MUTED}">{esc(sub)}</text>']
        self.L, self.R, self.T, self.B = L, W-R, T, H-B
        self.xr, self.yr = xr, yr
        self.xlabel, self.ylabel = xlabel, ylabel
    def X(self, x): return self.L + (x-self.xr[0])/(self.xr[1]-self.xr[0])*(self.R-self.L)
    def Y(self, y): return self.B - (y-self.yr[0])/(self.yr[1]-self.yr[0])*(self.B-self.T)
    def axes(self, xt, yt, yfmt=lambda v: f"{v:g}%", xfmt=lambda v: f"{v:g}h"):
        for v in yt:
            y = self.Y(v)
            self.s.append(f'<line x1="{self.L}" y1="{y:.1f}" x2="{self.R}" y2="{y:.1f}" stroke="{GRID}" stroke-width="1.5"/>')
            self.s.append(f'<text x="{self.L-14}" y="{y+8:.1f}" font-size="22" text-anchor="end" fill="{MUTED}">{yfmt(v)}</text>')
        for v in xt:
            x = self.X(v)
            self.s.append(f'<line x1="{x:.1f}" y1="{self.B}" x2="{x:.1f}" y2="{self.B+8}" stroke="{MUTED}" stroke-width="1.5"/>')
            self.s.append(f'<text x="{x:.1f}" y="{self.B+38}" font-size="22" text-anchor="middle" fill="{MUTED}">{xfmt(v)}</text>')
        self.s.append(f'<line x1="{self.L}" y1="{self.B}" x2="{self.R}" y2="{self.B}" stroke="{MUTED}" stroke-width="2"/>')
        self.s.append(f'<text x="{(self.L+self.R)/2:.1f}" y="{self.B+84}" font-size="23" text-anchor="middle" fill="{INK}">{esc(self.xlabel)}</text>')
        self.s.append(f'<text transform="translate(52 {(self.T+self.B)/2:.1f}) rotate(-90)" font-size="23" text-anchor="middle" fill="{INK}">{esc(self.ylabel)}</text>')
    def band_x(self, x0, x1, label, fill="#f3ead9"):
        self.s.insert(2, f'<rect x="{self.X(x0):.1f}" y="{self.T}" width="{self.X(x1)-self.X(x0):.1f}" height="{self.B-self.T}" fill="{fill}"/>')
        self.s.append(f'<text x="{(self.X(x0)+self.X(x1))/2:.1f}" y="{self.T+30}" font-size="21" text-anchor="middle" fill="#8a5a12">{esc(label)}</text>')
    def vline(self, x, label, dy=0):
        X = self.X(x)
        self.s.append(f'<line x1="{X:.1f}" y1="{self.T}" x2="{X:.1f}" y2="{self.B}" stroke="{MUTED}" stroke-width="2" stroke-dasharray="7 6"/>')
        self.s.append(f'<text x="{X+8:.1f}" y="{self.T+30+dy}" font-size="21" fill="{INK}">{esc(label)}</text>')
    def hline(self, y, label):
        Y = self.Y(y)
        self.s.append(f'<line x1="{self.L}" y1="{Y:.1f}" x2="{self.R}" y2="{Y:.1f}" stroke="{MUTED}" stroke-width="2" stroke-dasharray="4 6"/>')
        self.s.append(f'<text x="{self.R+12}" y="{Y+7:.1f}" font-size="20" fill="{MUTED}">{esc(label)}</text>')
    def ci(self, pts, color):
        top = " ".join(f"{self.X(x):.1f},{self.Y(hi):.1f}" for x, _, lo, hi in pts)
        bot = " ".join(f"{self.X(x):.1f},{self.Y(lo):.1f}" for x, _, lo, hi in reversed(pts))
        self.s.append(f'<polygon points="{top} {bot}" fill="{color}" fill-opacity="0.13"/>')
    def line(self, pts, color, label=None, dots=True, width=4, label_dy=0):
        d = " ".join(f"{self.X(x):.1f},{self.Y(y):.1f}" for x, y in pts)
        self.s.append(f'<polyline points="{d}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round" stroke-linecap="round"/>')
        if dots:
            for x, y in pts:
                self.s.append(f'<circle cx="{self.X(x):.1f}" cy="{self.Y(y):.1f}" r="7" fill="{BG}" stroke="{color}" stroke-width="3.5"/>')
        if label:
            x, y = pts[-1]
            self.s.append(f'<text x="{self.X(x)+16:.1f}" y="{self.Y(y)+8+label_dy:.1f}" font-size="23" font-weight="700" fill="{color}">{esc(label)}</text>')
    def note(self, x, y, text, anchor="start", size=21, color=None, weight="400"):
        self.s.append(f'<text x="{self.X(x):.1f}" y="{self.Y(y):.1f}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{color or INK}">{esc(text)}</text>')
    def save(self, name):
        self.s.append("</svg>")
        with open(os.path.join(FIG, name), "w", encoding="utf-8") as f:
            f.write("\n".join(self.s))

def write_csv(name, header, rows):
    with open(os.path.join(DAT, name), "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f); w.writerow(header); w.writerows(rows)

# ---------------------------------------------------------------------------
# 1. 스크립트 봇 상대 승률 (평가, 120라운드, 학습 정책 = 팀 0, 고정 진영)
BOT = [  # (누적 시간, 라벨, 승, 라운드, 환경)
    (1/3, "20분", 0, 120, "버그"), (2/3, "40분", 5, 120, "버그"), (1.0, "1시간", 5, 120, "버그"),
    (4/3, "1시간 20분", 38, 120, "버그"), (5/3, "1시간 40분", 79, 120, "버그"), (2.0, "2시간", 74, 120, "버그"),
    (7/3, "2시간 20분", 50, 120, "버그"), (8/3, "2시간 40분", 58, 120, "버그"), (3.0, "3시간", 66, 120, "버그"),
    (10/3, "3시간 20분", 84, 120, "버그"), (11/3, "3시간 40분", 93, 120, "버그"), (4.0, "4시간", 95, 120, "버그"),
    (4.0, "4시간 (수정 후 재측정)", 96, 120, "정상"), (5, "5시간", 89, 120, "정상"), (6, "6시간", 108, 120, "정상"),
    (7, "7시간", 87, 120, "정상"), (8, "8시간", 101, 120, "정상"), (9, "9시간", 112, 120, "정상"),
    (10, "10시간", 108, 120, "정상"), (11, "11시간", 114, 120, "정상"), (12, "12시간", 114, 120, "정상"),
    (13, "13시간", 117, 120, "정상"), (14, "14시간", 118, 120, "정상"), (15, "15시간", 121, 121, "정상"),
    (16, "16시간", 117, 120, "정상"),
]
rows = []
for x, lab, k, n, env in BOT:
    lo, hi = wilson(k, n)
    rows.append([f"{x:.2f}", lab, k, n, f"{100*k/n:.1f}", f"{lo:.1f}", f"{hi:.1f}", "여분 유닛 버그 환경" if env == "버그" else "정상 환경"])
write_csv("01_winrate_vs_bot.csv", ["누적 학습(시간)", "스냅샷", "승", "라운드", "승률(%)", "95% 하한", "95% 상한", "환경"], rows)

ch = Chart("학습한 정책 vs 스크립트 봇", "봇과는 한 번도 학습하지 않았다 (자기 자신과만 학습) · 스냅샷마다 120라운드 · 띠 = 95% 신뢰구간",
           (0, 16.5), (0, 100), "누적 학습 시간", "승률")
ch.band_x(0, 4, "버그 수정 전 측정")
ch.axes([0, 2, 4, 6, 8, 10, 12, 14, 16], [0, 25, 50, 75, 100])
ch.hline(50, "50%")
pts = []
for x, lab, k, n, env in BOT:
    if lab.startswith("4시간 ("):
        continue
    lo, hi = wilson(k, n)
    pts.append((x, 100*k/n, lo, hi))
ch.ci(pts, C[0])
ch.line([(x, y) for x, y, _, _ in pts], C[0])
ch.note(16.1, 97.5 - 8, "97.5%", size=24, color=C[0], weight="700")
ch.save("05_winrate_vs_bot.svg")

# ---------------------------------------------------------------------------
# 5. 시스템 구조도
def box(x, y, w, h, title, lines, fill="#eef3f8", stroke="#2f6fb0"):
    o = [f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="{fill}" stroke="{stroke}" stroke-width="3"/>',
         f'<text x="{x+w/2}" y="{y+42}" font-size="27" font-weight="700" text-anchor="middle" fill="{INK}">{esc(title)}</text>']
    for i, ln in enumerate(lines):
        o.append(f'<text x="{x+w/2}" y="{y+82+i*32}" font-size="21" text-anchor="middle" fill="{MUTED}">{esc(ln)}</text>')
    return o
def arrow(x1, y1, x2, y2, label="", dy=-12):
    o = [f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{INK}" stroke-width="3" marker-end="url(#a)"/>']
    if label:
        o.append(f'<text x="{(x1+x2)/2}" y="{(y1+y2)/2+dy}" font-size="20" text-anchor="middle" fill="{INK}">{esc(label)}</text>')
    return o
s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{FONT}">',
     '<defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#1d2633"/></marker></defs>',
     f'<rect width="{W}" height="{H}" fill="{BG}"/>',
     f'<text x="80" y="62" font-size="38" font-weight="700" fill="{INK}">학습 시스템 구조</text>',
     f'<text x="80" y="104" font-size="24" fill="{MUTED}">Unreal Engine 5.8 + Learning Agents 0.2 · 결정 20회/초 · PPO 학습은 별도 파이썬 프로세스(GPU)</text>',
     f'<rect x="60" y="140" width="1030" height="670" rx="18" fill="none" stroke="#c9d1da" stroke-width="2" stroke-dasharray="10 8"/>',
     f'<text x="84" y="178" font-size="22" font-weight="700" fill="{MUTED}">언리얼 엔진 (게임 시뮬레이션)</text>']
s += box(90, 200, 970, 170, "아레나 16개 × 유닛 20명 (10 대 10) = 에이전트 320", ["유닛: 시야 90° · 레이 16개, 히트스캔 총(0.15초 쿨다운), HP 100 · 한 발 10 피해", "라운드: 한 팀 전멸 또는 45초 · 20초부터 안전 구역 축소 · 두 팀 모두 같은 정책 (self-play)"])
s += box(90, 520, 300, 270, "행동", ["회전 7단계", "이동 5가지", "발사 예/아니오"], fill="#fbf1e6", stroke="#d9822b")
s += box(430, 520, 290, 270, "정책 신경망", ["언리얼 안에서 추론", "320 유닛이 공유", "결정 20회/초"], fill="#f3eefa", stroke="#8e5bb5")
s += box(760, 520, 300, 270, "관측", ["벡터 176", "시야 · 적 · 아군", "기억 · 팀 · 자신"], fill="#eef7f1", stroke="#3a9d5d")
s += box(1170, 330, 370, 250, "PPO 학습기 (파이썬)", ["버퍼 65,536 스텝마다 갱신", "torch 2.8 + CUDA 12.8", "RTX 5080", "1시간마다 스냅샷 저장"], fill="#f3eefa", stroke="#8e5bb5")
s += arrow(910, 372, 910, 516)
s.append(f'<text x="924" y="452" font-size="20" fill="{INK}">관측 수집</text>')
s += arrow(758, 655, 724, 655)
s += arrow(428, 655, 394, 655)
s += arrow(240, 516, 240, 374)
s.append(f'<text x="254" y="452" font-size="20" fill="{INK}">행동 실행</text>')
s += arrow(1062, 285, 1166, 400)
s.append(f'<text x="1120" y="262" font-size="20" fill="{INK}">경험 (관측 · 행동 · 보상)</text>')
s.append(f'<path d="M1355,582 L1355,840 L575,840 L575,796" fill="none" stroke="{INK}" stroke-width="3" marker-end="url(#a)"/>')
s.append(f'<text x="965" y="828" font-size="20" text-anchor="middle" fill="{INK}">갱신된 가중치</text>')
s.append("</svg>")
open(os.path.join(FIG, "01_architecture.svg"), "w", encoding="utf-8").write("\n".join(s))

def svg_open(title, sub):
    return [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="{FONT}">',
            '<defs><marker id="a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#1d2633"/></marker></defs>',
            f'<rect width="{W}" height="{H}" fill="{BG}"/>',
            f'<text x="80" y="62" font-size="38" font-weight="700" fill="{INK}">{esc(title)}</text>',
            f'<text x="80" y="104" font-size="24" fill="{MUTED}">{esc(sub)}</text>']

# ---------------------------------------------------------------------------
# 2. 아레나 (위에서 본 개략도, 실제 치수 비율)
s = svg_open("아레나와 라운드 규칙", "위에서 본 개략도 · 치수 비율은 실제와 같다 (반경 1,750 cm)")
cx, cy, sc = 520, 500, 0.21          # cm → px
def P(x, y): return cx + x*sc, cy - y*sc
R0 = 1750
oct_pts = " ".join(f"{P(R0/math.cos(math.pi/8)*math.cos(math.pi/8+k*math.pi/4), R0/math.cos(math.pi/8)*math.sin(math.pi/8+k*math.pi/4))[0]:.1f},{P(R0/math.cos(math.pi/8)*math.cos(math.pi/8+k*math.pi/4), R0/math.cos(math.pi/8)*math.sin(math.pi/8+k*math.pi/4))[1]:.1f}" for k in range(8))
s.append(f'<polygon points="{oct_pts}" fill="#f6f8fa" stroke="{INK}" stroke-width="6"/>')
s.append(f'<circle cx="{cx}" cy="{cy}" r="{600*sc:.1f}" fill="#fdecea" stroke="#c2410c" stroke-width="3" stroke-dasharray="10 7"/>')
s.append(f'<text x="{cx}" y="{cy-600*sc-14:.1f}" font-size="20" text-anchor="middle" fill="#c2410c">최종 안전 구역 600 cm</text>')
x0, y0 = P(-200, 200)
s.append(f'<rect x="{x0:.1f}" y="{y0:.1f}" width="{400*sc:.1f}" height="{400*sc:.1f}" fill="#9aa4ae"/>')
s.append(f'<text x="{cx}" y="{cy+6}" font-size="18" text-anchor="middle" fill="#ffffff">엄폐물</text>')
for team, base, col in ((0, 180, C[0]), (1, 0, C[4])):
    for i in range(10):
        a = math.radians(base + (i - 4.5) * 12)
        x, y = P(1200*math.cos(a), 1200*math.sin(a))
        s.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="9" fill="{col}"/>')
s.append(f'<text x="{P(-1200,0)[0]-30:.1f}" y="{P(0,1450)[1]:.1f}" font-size="22" font-weight="700" text-anchor="middle" fill="{C[0]}">팀 0 (10명)</text>')
s.append(f'<text x="{P(1200,0)[0]+30:.1f}" y="{P(0,1450)[1]:.1f}" font-size="22" font-weight="700" text-anchor="middle" fill="{C[4]}">팀 1 (10명)</text>')
# 한 유닛의 시야 부채꼴 (90°, 레이 16개)
ux, uy = P(-1200*math.cos(math.radians(12*0.5)), -1200*math.sin(math.radians(12*0.5)))
for k in range(16):
    a = math.radians(-45 + 90*(k+0.5)/16)
    s.append(f'<line x1="{ux:.1f}" y1="{uy:.1f}" x2="{ux+200*math.cos(a):.1f}" y2="{uy-200*math.sin(a):.1f}" stroke="{C[2]}" stroke-width="2" stroke-opacity="0.8"/>')
s.append(f'<text x="{cx:.1f}" y="{cy+235:.1f}" font-size="20" text-anchor="middle" fill="{C[2]}">왼쪽 한 유닛의 시야: 90°, 레이 16개</text>')
rx = 1000
rules = [("한 라운드", "한 팀 전멸 또는 45초"),
         ("시간 초과", "양쪽 모두 패배 (TFT 와 같은 규칙)"),
         ("축소 구역", "20초부터 15초 동안 1,750 → 600 cm,\n밖에 있으면 초당 10 피해"),
         ("스폰", "팀당 10명이 호 모양으로, 중심을 보고 시작\n위치 ±3° / ±100 cm 흔들림"),
         ("진영 교대", "학습 중 라운드마다 두 팀 위치를 맞바꿈"),
         ("규모", "이런 아레나 16개를 동시에 실행")]
y = 190
for k, v in rules:
    s.append(f'<text x="{rx}" y="{y}" font-size="25" font-weight="700" fill="{INK}">{esc(k)}</text>')
    for j, ln in enumerate(v.split("\n")):
        s.append(f'<text x="{rx}" y="{y+36+j*32}" font-size="22" fill="{MUTED}">{esc(ln)}</text>')
    y += 72 + 32*len(v.split("\n"))
s.append(f'<text x="{rx}" y="{H-40}" font-size="18" fill="{MUTED}">엄폐물은 7개이며 그림에는 중앙 1개만 표시했다.</text>')
s.append("</svg>")
open(os.path.join(FIG, "02_arena.svg"), "w", encoding="utf-8").write("\n".join(s))

# ---------------------------------------------------------------------------
# 3. 유닛 하나의 입력과 출력
s = svg_open("유닛이 보는 것과 하는 것", "320개 유닛이 같은 신경망 하나를 공유한다 · 0.05초마다 관측 → 행동")
obs = [("시야 레이 16개", "벽 · 엄폐물 · 적 · 아군까지 거리"),
       ("보이는 적 (최대 10)", "방향 · 거리 · HP · 나를 보고 있는지"),
       ("아군 (최대 9)", "방향 · 거리 · HP"),
       ("기억", "마지막으로 적을 본 방향과 경과 시간"),
       ("피격 기억", "마지막으로 맞은 방향과 경과 시간"),
       ("팀 · 라운드", "양 팀 생존 비율 · 경과 · 안전 구역"),
       ("자신", "HP · 지금 쏠 수 있는지")]
y = 160
for t, d in obs:
    s.append(f'<rect x="80" y="{y}" width="560" height="86" rx="12" fill="#eef7f1" stroke="{C[2]}" stroke-width="2.5"/>')
    s.append(f'<text x="104" y="{y+36}" font-size="24" font-weight="700" fill="{INK}">{esc(t)}</text>')
    s.append(f'<text x="104" y="{y+68}" font-size="20" fill="{MUTED}">{esc(d)}</text>')
    s += arrow(642, y+43, 700, 500 if False else y+43 + (480 - (y+43))*0.6, "")
    y += 98
s.append(f'<rect x="710" y="350" width="260" height="260" rx="20" fill="#f3eefa" stroke="{C[3]}" stroke-width="3"/>')
s.append(f'<text x="840" y="455" font-size="28" font-weight="700" text-anchor="middle" fill="{INK}">신경망</text>')
s.append(f'<text x="840" y="495" font-size="21" text-anchor="middle" fill="{MUTED}">(정책)</text>')
s.append(f'<text x="840" y="535" font-size="19" text-anchor="middle" fill="{MUTED}">입력 벡터 176</text>')
acts = [("회전", "7단계 각속도", "−180 · −45 · −10 · 0 · +10 · +45 · +180 °/s"),
        ("이동", "5가지", "정지 · 전진 · 후진 · 왼쪽 · 오른쪽 (조준선 기준)"),
        ("발사", "예 / 아니오", "쿨다운 0.15초 중에는 쏴도 나가지 않음")]
y = 250
for t, d, e in acts:
    s += arrow(972, 480, 1040, y+55, "")
    s.append(f'<rect x="1045" y="{y}" width="490" height="120" rx="12" fill="#fbf1e6" stroke="{C[1]}" stroke-width="2.5"/>')
    s.append(f'<text x="1070" y="{y+40}" font-size="25" font-weight="700" fill="{INK}">{esc(t)} <tspan font-weight="400" fill="{MUTED}">· {esc(d)}</tspan></text>')
    s.append(f'<text x="1070" y="{y+80}" font-size="19" fill="{MUTED}">{esc(e)}</text>')
    y += 150
s.append(f'<text x="1045" y="{y+20}" font-size="20" fill="{MUTED}">적 위치는 자기 시야로만 안다 (전지적 정보 없음)</text>')
s.append("</svg>")
open(os.path.join(FIG, "03_agent_io.svg"), "w", encoding="utf-8").write("\n".join(s))

# ---------------------------------------------------------------------------
# 4. 결정 한 번의 흐름 (0.05초)
s = svg_open("학습이 돌아가는 방식", "0.05초마다 아래 고리가 한 바퀴 돈다 · 320개 유닛이 동시에")
steps = [("① 관측", "시야 스캔 후\n관측 벡터 176 수집"), ("② 행동 선택", "신경망이 회전 · 이동 ·\n발사를 확률로 고름"),
         ("③ 실행", "프레임마다 회전 · 이동\n총알 판정, HP 감소"), ("④ 보상", "준 피해 − 받은 피해,\n킬 · 사망 · 팀 평균"),
         ("⑤ 기록", "(관측, 행동, 보상)을\n버퍼에 저장")]
bw, bh, gx, y0 = 250, 170, 52, 230
for i, (t, d) in enumerate(steps):
    x = 80 + i*(bw+gx)
    s.append(f'<rect x="{x}" y="{y0}" width="{bw}" height="{bh}" rx="16" fill="#eef3f8" stroke="{C[0]}" stroke-width="3"/>')
    s.append(f'<text x="{x+bw/2}" y="{y0+50}" font-size="27" font-weight="700" text-anchor="middle" fill="{INK}">{esc(t)}</text>')
    for j, ln in enumerate(d.split("\n")):
        s.append(f'<text x="{x+bw/2}" y="{y0+95+j*32}" font-size="21" text-anchor="middle" fill="{MUTED}">{esc(ln)}</text>')
    if i < len(steps)-1:
        s += arrow(x+bw+4, y0+bh/2, x+bw+gx-4, y0+bh/2)
# 되돌아가는 화살표
xl, xr_ = 80 + bw/2, 80 + 4*(bw+gx) + bw/2
s.append(f'<path d="M{xr_},{y0+bh+4} L{xr_},{y0+bh+60} L{xl},{y0+bh+60} L{xl},{y0+bh+8}" fill="none" stroke="{INK}" stroke-width="3" marker-end="url(#a)"/>')
s.append(f'<text x="{(xl+xr_)/2}" y="{y0+bh+92}" font-size="21" text-anchor="middle" fill="{INK}">다음 0.05초</text>')
by = 600
s.append(f'<rect x="80" y="{by}" width="1440" height="200" rx="16" fill="#f3eefa" stroke="{C[3]}" stroke-width="3"/>')
s.append(f'<text x="110" y="{by+50}" font-size="27" font-weight="700" fill="{INK}">버퍼가 65,536 스텝 차면 — PPO 갱신 (파이썬, GPU)</text>')
for j, ln in enumerate(["좋은 결과로 이어진 행동의 확률을 높이고, 나쁜 결과의 확률을 낮추도록 신경망 가중치를 조정한다",
                        "두 팀이 모두 같은 신경망이다 (self-play) → 상대도 함께 강해진다",
                        "1시간마다 가중치를 Snapshots/<날짜>/cum<누적 시간>/ 에 저장 · 저장한 것에서 이어서 학습 · 고정 상대와 평가"]):
    s.append(f'<text x="110" y="{by+95+j*36}" font-size="22" fill="{MUTED}">{esc(ln)}</text>')
s.append("</svg>")
open(os.path.join(FIG, "04_training_loop.svg"), "w", encoding="utf-8").write("\n".join(s))

# ---------------------------------------------------------------------------
# 6. 표 데이터
write_csv("02_reward_terms.csv", ["항", "값", "언제", "비고"], [
    ["준 피해", "+1.0 / 피해 100", "명중할 때", "제로섬"],
    ["받은 피해", "−1.0 / 피해 100", "맞을 때", "제로섬 (이전 0.7)"],
    ["킬", "+0.5", "마지막 명중자", "사망 벌점과 같은 크기"],
    ["사망", "−0.5", "죽은 스텝 한 번", "이전 1.0 → 위험 회피 유발해 낮춤"],
    ["빗맞힘", "−0.005 / 발", "빗나갈 때", "0.05 에서 발사량 10배 감소 → 낮춤"],
    ["시간", "−0.002 / 결정", "살아 있는 동안", ""],
    ["적 보임 (셰이핑)", "+0.02 × 배율", "적이 시야에 있을 때", "배율 1 → 0.15 로 감쇠"],
    ["조준 정렬 (셰이핑)", "+0.05 × 정렬도⁴ × 배율", "적이 시야에 있을 때", "배율 1 → 0.15 로 감쇠"],
    ["팀 스피릿 τ", "0.3", "매 결정", "보상 = 0.7·개인 + 0.3·같은 팀 평균"],
    ["라운드 승패", "±2.0 (현재 self-play 에서는 0)", "라운드 끝", "같은 정책끼리 승패는 잡음 → 끔"],
])
print("ok")
