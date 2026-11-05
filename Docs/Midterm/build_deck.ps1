# 중간발표 덱(.pptx)을 PowerPoint COM 으로 만든다. 사용: powershell -ExecutionPolicy Bypass -File build_deck.ps1
# 결과: Docs/Midterm/TEAM5_midterm.pptx, 검사용 슬라이드 이미지 Docs/Midterm/preview/
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Fig  = Join-Path $Root "figures\deck"
$Out  = Join-Path $Root "TEAM5_midterm.pptx"
$Prev = Join-Path $Root "preview"

function RGB($r, $g, $b) { return [int]($r + 256 * $g + 65536 * $b) }
$INK    = RGB 29 38 51
$MUTED  = RGB 95 107 120
$BLUE   = RGB 47 111 176
$ORANGE = RGB 217 130 43
$GREEN  = RGB 58 157 93
$PURPLE = RGB 142 91 181
$TINT   = RGB 238 243 248
$WHITE  = RGB 255 255 255
$FONT   = "맑은 고딕"

$wasRunning = [bool](Get-Process POWERPNT -ErrorAction SilentlyContinue)
$app = New-Object -ComObject PowerPoint.Application
$app.Visible = -1
$app.WindowState = 2                       # 최소화 — 차트는 창이 있는 프레젠테이션에서만 만들어진다
$pres = $app.Presentations.Add(-1)
trap {
  Write-Host ("ERROR line " + $_.InvocationInfo.ScriptLineNumber + ": " + $_.Exception.Message)
  try { if ($wb) { $wb.Close() } } catch {}
  try { $pres.Close() } catch {}
  if (-not $wasRunning) { try { $app.Quit() } catch {} }
  throw
}
$pres.PageSetup.SlideWidth  = 960          # 13.333 in (16:9)
$pres.PageSetup.SlideHeight = 540          # 7.5 in

# --- 테마: 글꼴과 색을 한 곳에 둔다 (디자인 → 변형에서 바꾸면 전체가 바뀐다) ---
$theme = $pres.SlideMaster.Theme
# ★ 라틴 글꼴까지 맑은 고딕으로 두면 한글이 단어 중간에서 줄바꿈된다(실측). 라틴은 Segoe UI 로 둔다.
$theme.ThemeFontScheme.MajorFont.Item(1).Name = "Segoe UI"
$theme.ThemeFontScheme.MinorFont.Item(1).Name = "Segoe UI"
foreach ($slot in 2, 3) {                  # 복합 · 동아시아
  $theme.ThemeFontScheme.MajorFont.Item($slot).Name = $FONT
  $theme.ThemeFontScheme.MinorFont.Item($slot).Name = $FONT
}
$cs = $theme.ThemeColorScheme
$cs.Colors(1).RGB = $INK;  $cs.Colors(2).RGB = $WHITE   # dk1 / lt1
$cs.Colors(3).RGB = $MUTED; $cs.Colors(4).RGB = $TINT   # dk2 / lt2
$cs.Colors(5).RGB = $BLUE; $cs.Colors(6).RGB = $ORANGE; $cs.Colors(7).RGB = $GREEN
$cs.Colors(8).RGB = $PURPLE; $cs.Colors(9).RGB = (RGB 194 65 12); $cs.Colors(10).RGB = (RGB 108 120 132)

function Get-Layout($name) {
  foreach ($l in $pres.SlideMaster.CustomLayouts) { if ($l.Name -eq $name) { return $l } }
  return $pres.SlideMaster.CustomLayouts.Item(1)
}
# 기본 테마의 레이아웃: 1 = 제목 슬라이드, 6 = 제목만
$LayTitle = $pres.SlideMaster.CustomLayouts.Item(1)
$LayTitleOnly = $pres.SlideMaster.CustomLayouts.Item(6)

function New-ContentSlide($index, $title) {
  $s = $pres.Slides.AddSlide($index, $LayTitleOnly)
  $t = $s.Shapes.Placeholders.Item(1)
  $t.Left = 40; $t.Top = 22; $t.Width = 880; $t.Height = 56
  $r = $t.TextFrame.TextRange
  $r.Text = $title
  $r.Font.Size = 32; $r.Font.Bold = -1; $r.Font.Color.RGB = $INK
  $r.ParagraphFormat.Alignment = 1
  $r.ParagraphFormat.WordWrap = -1
  $t.TextFrame.VerticalAnchor = 3
  return $s
}
function Add-Text($s, $name, $text, $l, $t, $w, $h, $size, $color, [bool]$bold = $false, $align = 1) {
  $shp = $s.Shapes.AddTextbox(1, $l, $t, $w, $h)
  $shp.Name = $name
  $tf = $shp.TextFrame
  $tf.WordWrap = -1; $tf.AutoSize = 0
  $tf.MarginLeft = 0; $tf.MarginRight = 0; $tf.MarginTop = 0; $tf.MarginBottom = 0
  $r = $tf.TextRange
  $r.Text = $text
  $r.Font.Size = $size; $r.Font.Color.RGB = $color; $r.Font.Bold = [int]$bold
  $r.ParagraphFormat.Alignment = $align
  # 한글을 띄어쓰기 단위로 줄바꿈한다. 동아시아 글꼴·한국어를 지정하지 않으면 글자 단위로 잘린다.
  $r.Font.NameFarEast = $FONT
  $r.LanguageID = 1042
  $r.ParagraphFormat.WordWrap = -1
  return $shp
}
function Add-Bullets($s, $name, [string[]]$lines, $l, $t, $w, $h, $size) {
  $shp = Add-Text $s $name ($lines -join "`r") $l $t $w $h $size $INK
  $r = $shp.TextFrame.TextRange
  $r.ParagraphFormat.Bullet.Visible = -1
  $r.ParagraphFormat.Bullet.Character = 8226
  $r.ParagraphFormat.SpaceAfter = 10
  $shp.TextFrame.Ruler.Levels.Item(1).FirstMargin = 0
  $shp.TextFrame.Ruler.Levels.Item(1).LeftMargin = 18
  return $shp
}
function Add-Card($s, $name, $l, $t, $w, $h, $fill) {
  $c = $s.Shapes.AddShape(5, $l, $t, $w, $h)
  $c.Name = $name
  $c.Adjustments.Item(1) = 0.08
  $c.Fill.ForeColor.RGB = $fill
  $c.Line.Visible = 0
  $c.Shadow.Visible = 0
  return $c
}
function Add-Figure($s, $file, $top, $height) {
  $w = $height * 1600 / 780
  $p = $s.Shapes.AddPicture((Join-Path $Fig $file), 0, -1, (960 - $w) / 2, $top, $w, $height)
  $p.Name = "그림 " + $file
  return $p
}
function Set-Notes($s, $text) { $s.NotesPage.Shapes.Placeholders.Item(2).TextFrame.TextRange.Text = $text }

# ============================================================================
# 1. 제목
$s = $pres.Slides.AddSlide(1, $LayTitle)
$s.FollowMasterBackground = 0
$s.Background.Fill.ForeColor.RGB = $INK
$t = $s.Shapes.Placeholders.Item(1)
$t.Left = 60; $t.Top = 170; $t.Width = 840; $t.Height = 130
$t.TextFrame.TextRange.Text = "10 대 10 오토배틀러 전투 AI"
$t.TextFrame.TextRange.Font.Size = 44; $t.TextFrame.TextRange.Font.Bold = -1; $t.TextFrame.TextRange.Font.Color.RGB = $WHITE
$t.TextFrame.TextRange.ParagraphFormat.Alignment = 1
$t.TextFrame.VerticalAnchor = 4
$sub = $s.Shapes.Placeholders.Item(2)
$sub.Left = 60; $sub.Top = 315; $sub.Width = 840; $sub.Height = 90
$sub.TextFrame.TextRange.Text = "강화학습으로 유닛 전투(조준 · 이동 · 사격) 학습`rHAI 2026-1 TEAM 5  ·  Unreal Engine 5.8  ·  Learning Agents 0.2"
$sub.TextFrame.TextRange.Font.Size = 20; $sub.TextFrame.TextRange.Font.Color.RGB = (RGB 202 214 228)
$sub.TextFrame.TextRange.Paragraphs(2).Font.Size = 16
$sub.TextFrame.TextRange.ParagraphFormat.Alignment = 1
$sub.TextFrame.TextRange.ParagraphFormat.WordWrap = -1
$sub.TextFrame.VerticalAnchor = 1
Set-Notes $s "TFT 같은 오토배틀러에서 유닛이 스스로 싸우는 방법을 강화학습으로 배우게 만든 과정을 소개한다."

# ============================================================================
# 2. 무엇을 만들었나
$s = New-ContentSlide 2 "무엇을 만들었나"
$stats = @(
  @("320", "에이전트`r아레나 16개 × 유닛 20명", $BLUE),
  @("0.05초", "마다 한 번 결정`r회전 · 이동 · 발사", $ORANGE),
  @("176", "개 숫자를 보고 판단`r자기 시야로 본 것만", $GREEN),
  @("16시간", "누적 학습`r사람이 만든 정답 없이", $PURPLE))
for ($i = 0; $i -lt 4; $i++) {
  $l = 40 + $i * 226
  $card = Add-Card $s ("카드 " + ($i + 1)) $l 104 206 132 $TINT
  $n = Add-Text $s ("숫자 " + ($i + 1)) $stats[$i][0] ($l + 18) 122 172 56 40 $stats[$i][2] $true
  $d = Add-Text $s ("설명 " + ($i + 1)) $stats[$i][1] ($l + 18) 184 172 76 15 $INK
  $d.TextFrame.TextRange.Paragraphs(2).Font.Color.RGB = $MUTED
  $d.TextFrame.TextRange.Paragraphs(2).Font.Size = 13
}
$b = Add-Bullets $s "핵심 내용" @(
  "유닛 하나하나가 조준 · 이동 · 사격을 스스로 결정하는 신경망 정책",
  "이 정책을 학습시키는 환경: 팔각형 아레나에서 10 대 10 라운드를 16판 동시에 진행",
  "같은 정책끼리 싸우며 배운다 (self-play) — 녹화 데이터나 사람이 짠 행동 규칙 없음") 40 280 880 200 20
Set-Notes $s "사람이 행동을 짜 주는 대신, 유닛이 매 0.05초마다 보이는 정보를 보고 회전·이동·발사를 고른다. 좋은 결과가 나온 선택을 더 자주 하도록 반복해서 고친다. 지금까지 누적 16시간 학습했다."

# ============================================================================
# 3. 전체 구조
$s = New-ContentSlide 3 "전체 구조"
Add-Figure $s "01_architecture.png" 88 430 | Out-Null
Set-Notes $s "왼쪽 큰 상자가 언리얼 엔진 안이다. 유닛이 관측을 만들고, 신경망이 행동을 고르고, 행동이 다시 게임에 반영된다. 그 기록이 쌓이면 오른쪽 파이썬 학습기(PPO, GPU)가 신경망을 고쳐서 돌려보낸다. 320개 유닛이 신경망 하나를 공유한다."

# ============================================================================
# 4. 아레나와 라운드 규칙
$s = New-ContentSlide 4 "아레나와 라운드 규칙"
Add-Figure $s "02_arena.png" 88 430 | Out-Null
Set-Notes $s "같은 실력끼리 싸우면 서로 엄폐물 뒤에서 버티기만 하는 교착이 생길 수 있다. 시간 초과를 양쪽 패배로 두고 안전 구역을 좁혀서 결국 싸우게 만들었다. 실제로 시간 초과 비율은 0% 근처다. 학습 중에는 라운드마다 두 팀의 진영을 맞바꿔서 한쪽 진영에만 익숙해지지 않게 한다."

# ============================================================================
# 5. 유닛이 보는 것과 하는 것
$s = New-ContentSlide 5 "유닛이 보는 것과 하는 것"
Add-Figure $s "03_agent_io.png" 84 410 | Out-Null
$k = Add-Text $s "핵심 설계" "핵심 설계: 시야 레이와 총알이 같은 충돌 판정을 쓴다 → 「보인다」 = 「쏘면 맞는다」" 40 500 880 24 16 $BLUE $true 2
Set-Notes $s "적 좌표를 그냥 주지 않고 자기 시야로 본 것만 준다. 엄폐물 뒤의 적은 안 보이고, 그래서 엄폐가 의미를 가진다. 행동은 0.05초마다 회전 7단계, 이동 5가지, 발사 여부를 고른다."

# ============================================================================
# 6. 보상
$s = New-ContentSlide 6 "보상 — 무엇을 잘했다고 알려 주는가"
$rows = @(
  @("항목", "값"),
  @("준 피해", "+1.0 / 피해 100"),
  @("받은 피해", "−1.0 / 피해 100"),
  @("킬 / 사망", "+0.5 / −0.5"),
  @("빗맞힘", "−0.005 / 발"),
  @("생존 중 시간", "−0.002 / 결정"),
  @("적을 시야에 둠 · 조준 정렬", "소량, 학습이 진행되며 줄어듦"),
  @("팀 스피릿", "0.7 × 내 보상 + 0.3 × 팀 평균"))
$tb = $s.Shapes.AddTable($rows.Count, 2, 40, 100, 520, 360)
$tb.Name = "보상 표"
$tbl = $tb.Table
$tbl.Columns.Item(1).Width = 250; $tbl.Columns.Item(2).Width = 270
for ($r = 1; $r -le $rows.Count; $r++) {
  for ($c = 1; $c -le 2; $c++) {
    $cell = $tbl.Cell($r, $c)
    $tr = $cell.Shape.TextFrame.TextRange
    $tr.Text = $rows[$r - 1][$c - 1]
    $tr.Font.Size = 15
    $tr.ParagraphFormat.WordWrap = -1
    $cell.Shape.TextFrame.VerticalAnchor = 3
    $cell.Shape.TextFrame.MarginLeft = 10
    if ($r -eq 1) {
      $cell.Shape.Fill.ForeColor.RGB = $INK; $tr.Font.Color.RGB = $WHITE; $tr.Font.Bold = -1
    } else {
      $cell.Shape.Fill.ForeColor.RGB = $(if ($r % 2 -eq 0) { $WHITE } else { $TINT })
      $tr.Font.Color.RGB = $INK; $tr.Font.Bold = [int]($c -eq 1)
    }
  }
}
$c1 = Add-Card $s "설계 원칙 1" 590 100 330 170 $TINT
Add-Text $s "원칙 1 제목" "주는 것과 받는 것을 같은 크기로" 608 116 294 26 18 $BLUE $true | Out-Null
Add-Text $s "원칙 1 설명" "준 피해와 받은 피해, 킬과 사망을 같은 값으로 맞췄다. 한쪽이 크면 지나치게 몸을 사리거나 무모해진다." 608 150 294 110 15 $INK | Out-Null
$c2 = Add-Card $s "설계 원칙 2" 590 290 330 170 $TINT
Add-Text $s "원칙 2 제목" "죽어도 팀 보상은 받는다" 608 306 294 26 18 $ORANGE $true | Out-Null
Add-Text $s "원칙 2 설명" "죽은 유닛도 라운드 끝까지 팀 평균 보상을 받는다. 팀을 위한 희생을 배울 수 있게 하기 위해서다." 608 340 294 110 15 $INK | Out-Null
Set-Notes $s "빗맞힘 벌점을 0.05 로 크게 줬을 때는 쏘는 것 자체가 손해가 돼서 유닛이 사격을 거의 멈췄다. 그래서 아주 작게 남겼다. 적을 시야에 두거나 조준을 맞추는 보상은 학습 초반에 방향을 잡아 주는 용도이고, 점점 줄어든다."

# ============================================================================
# 7. 학습이 돌아가는 방식
$s = New-ContentSlide 7 "학습이 돌아가는 방식"
Add-Figure $s "04_training_loop.png" 88 430 | Out-Null
Set-Notes $s "0.05초마다 관측 → 행동 선택 → 실행 → 보상 → 기록이 한 바퀴 돈다. 기록이 65,536개 쌓이면 PPO 로 신경망을 갱신한다. 두 팀이 같은 신경망이라 상대도 함께 강해진다. 1시간마다 신경망을 저장하고, 저장한 것에서 이어서 학습하거나 과거 버전과 대결시켜 평가한다. 학습은 화면 없이 최고 속도로 돌리고, 확인할 때만 시각화 전용 레벨(Lvl_SHView)에서 실제 속도로 본다."

# ============================================================================
# 8. 실제로 동작하는가 — 기본 차트
$s = New-ContentSlide 8 "실제로 동작하는가"
$hours = @(0.33, 0.67, 1, 1.33, 1.67, 2, 2.33, 2.67, 3, 3.33, 3.67, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16)
$rates = @(0, 4.2, 4.2, 31.7, 65.8, 61.7, 41.7, 48.3, 55.0, 70.0, 77.5, 79.2, 74.2, 90.0, 72.5, 84.2, 93.3, 90.0, 95.0, 95.0, 97.5, 98.3, 100.0, 97.5)
$chShape = $s.Shapes.AddChart2(-1, 74, 40, 92, 560, 400, $true)    # 74 = 분산형(직선과 표식)
$chShape.Name = "봇 상대 승률 차트"
$ch = $chShape.Chart
$ch.ChartData.Activate()
# 차트 데이터 통합 문서(Excel)는 비동기로 열린다 — 준비될 때까지 기다린다
$wb = $null
for ($try = 0; $try -lt 40 -and -not $wb; $try++) { Start-Sleep -Milliseconds 250; try { $wb = $ch.ChartData.Workbook } catch {} }
$ws = $wb.Worksheets.Item(1)
$ws.Cells.Clear() | Out-Null
$ws.Range("A1").Value2 = "누적 학습(시간)"
$ws.Range("B1").Value2 = "스크립트 봇 상대 승률(%)"
for ($i = 0; $i -lt $hours.Count; $i++) {
  $ws.Range("A$($i + 2)").Value2 = [double]$hours[$i]
  $ws.Range("B$($i + 2)").Value2 = [double]$rates[$i]
}
$ch.SetSourceData("='Sheet1'!`$A`$1:`$B`$$($hours.Count + 1)")
$wb.Application.WindowState = -4140
$wb.Close()
while ($ch.SeriesCollection().Count -gt 1) { $ch.SeriesCollection(2).Delete() }
$ch.HasTitle = $true
$ch.ChartTitle.Text = "스크립트 봇 상대 승률 (스냅샷마다 120라운드)"
$ch.ChartTitle.Format.TextFrame2.TextRange.Font.Size = 14
$ch.ChartTitle.Format.TextFrame2.TextRange.Font.Bold = 0
$ch.HasLegend = $false
$ser = $ch.SeriesCollection(1)
$ser.Format.Line.ForeColor.RGB = $BLUE
$ser.Format.Line.Weight = 2.5
$ser.MarkerStyle = 8; $ser.MarkerSize = 6
$ser.MarkerBackgroundColor = $WHITE; $ser.MarkerForegroundColor = $BLUE
$ax = $ch.Axes(1); $ax.MinimumScale = 0; $ax.MaximumScale = 16; $ax.MajorUnit = 2
$ax.HasTitle = $true; $ax.AxisTitle.Text = "누적 학습 시간 (h)"
$ay = $ch.Axes(2); $ay.MinimumScale = 0; $ay.MaximumScale = 100; $ay.MajorUnit = 25
$ay.HasTitle = $true; $ay.AxisTitle.Text = "승률 (%)"
$ay.MajorGridlines.Format.Line.ForeColor.RGB = (RGB 227 231 236)

Add-Card $s "결과 카드 1" 630 92 290 120 $TINT | Out-Null
Add-Text $s "결과 1 숫자" "97.5%" 648 104 254 50 38 $BLUE $true | Out-Null
Add-Text $s "결과 1 설명" "누적 16시간 정책의 봇 상대 승률. 봇과는 한 번도 학습하지 않았다" 648 156 254 50 13 $INK | Out-Null
Add-Card $s "결과 카드 2" 630 228 290 120 $TINT | Out-Null
Add-Text $s "결과 2 숫자" "87%" 648 240 254 50 38 $ORANGE $true | Out-Null
Add-Text $s "결과 2 설명" "11시간 때의 자기 자신을 이기는 비율. 아직도 강해지는 중" 648 292 254 50 13 $INK | Out-Null
Add-Card $s "시연 카드" 630 364 290 128 $TINT | Out-Null
Add-Text $s "시연 제목" "시연 영상" 648 376 254 24 16 $GREEN $true | Out-Null
Add-Text $s "시연 설명" "Lvl_SHView 에서 Play — 팀 색(파랑·빨강)과 시야 레이가 보인다. (영상 삽입 자리)" 648 404 254 80 13 $INK | Out-Null
Set-Notes $s "사람이 규칙으로 짠 스크립트 봇과 대결시킨 결과다. 정책은 봇과 한 번도 학습하지 않았고 자기 자신과만 학습했다. 학습 1시간 20분에 처음 이기기 시작해 누적 16시간에 97.5%가 됐다. 4시간 이전 값은 나중에 고친 버그가 있던 환경에서 잰 것이다(고친 뒤 4시간 정책 재측정 79 → 80%로 거의 같음). 과거의 자기 자신(11시간 정책)도 87% 이긴다."

# ============================================================================
# 9. 다음 단계
$s = New-ContentSlide 9 "다음 단계"
$next = @(
  @("동시 사격 판정", "같은 순간 서로 쏘면 먼저 처리되는 팀이 이기는 순서 문제를 고친다", $BLUE),
  @("비교 실험 환경", "보상 값만 바꾼 여러 학습을 동시에 돌려 결과를 비교한다", $ORANGE),
  @("게임에 탑재", "실제 게임 보드(TFT 식 배치)에서 학습된 정책으로 유닛을 움직인다", $GREEN))
for ($i = 0; $i -lt 3; $i++) {
  $l = 40 + $i * 300
  Add-Card $s ("다음 카드 " + ($i + 1)) $l 120 280 230 $TINT | Out-Null
  $num = $s.Shapes.AddShape(9, $l + 20, 142, 44, 44)
  $num.Name = "번호 " + ($i + 1)
  $num.Fill.ForeColor.RGB = $next[$i][2]; $num.Line.Visible = 0
  $num.TextFrame.TextRange.Text = [string]($i + 1)
  $num.TextFrame.TextRange.Font.Size = 20; $num.TextFrame.TextRange.Font.Bold = -1; $num.TextFrame.TextRange.Font.Color.RGB = $WHITE
  Add-Text $s ("다음 제목 " + ($i + 1)) $next[$i][0] ($l + 20) 204 240 30 20 $INK $true | Out-Null
  Add-Text $s ("다음 설명 " + ($i + 1)) $next[$i][1] ($l + 20) 244 240 96 16 $MUTED | Out-Null
}
Set-Notes $s "남은 일 세 가지다. 첫째, 결정마다 행동이 번호 순서로 처리되어 같은 순간 서로 쏘면 팀 0 이 먼저 맞히는 문제가 있어 동시 판정으로 고친다. 둘째, 보상 값을 바꾼 학습 여러 개를 동시에 돌려 비교하는 환경을 만든다. 셋째, 실제 게임 보드에 학습된 정책을 탑재한다."

# ============================================================================
$pres.SaveAs($Out, 24)
if (Test-Path $Prev) { Remove-Item $Prev -Recurse -Force }
New-Item -ItemType Directory -Force $Prev | Out-Null
for ($i = 1; $i -le $pres.Slides.Count; $i++) {
  $pres.Slides.Item($i).Export((Join-Path $Prev ("slide{0:D2}.png" -f $i)), "PNG", 1600, 900)
}
$count = $pres.Slides.Count
$pres.Close()
if (-not $wasRunning) { $app.Quit() }
# COM 참조를 풀어야 PowerPoint 프로세스가 실제로 끝난다
[void][Runtime.InteropServices.Marshal]::ReleaseComObject($app)
[GC]::Collect(); [GC]::WaitForPendingFinalizers()
"saved $Out ($count slides)"
