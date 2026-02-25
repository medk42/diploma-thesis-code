import cv2
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import A4, landscape
from reportlab.lib.units import mm
from reportlab.lib.utils import ImageReader
from io import BytesIO

# ===== Your settings =====
ROW_COUNT = 8
COL_COUNT = 12
SQUARE_LENGTH_M = 0.024
MARKER_LENGTH_M = 0.018
DICT = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_100)
LEGACY_PATTERN = True

# ===== Output =====
DPI = 600
OUT_PDF = "charuco_A4_landscape_exact.pdf"

# Physical board size (mm)
square_mm = SQUARE_LENGTH_M * 1000.0
board_w_mm = COL_COUNT * square_mm   # 288
board_h_mm = ROW_COUNT * square_mm   # 192

# Create board
board = cv2.aruco.CharucoBoard((COL_COUNT, ROW_COUNT),
                               float(SQUARE_LENGTH_M),
                               float(MARKER_LENGTH_M),
                               DICT)
board.setLegacyPattern(bool(LEGACY_PATTERN))

# Raster size matching physical size
w_px = int(round(board_w_mm / 25.4 * DPI))
h_px = int(round(board_h_mm / 25.4 * DPI))

# IMPORTANT: marginSize=0 so squares stay exact
img = board.generateImage((w_px, h_px), marginSize=0, borderBits=1)

# Encode PNG in-memory
ok, png = cv2.imencode(".png", img)
if not ok:
    raise RuntimeError("PNG encoding failed")
buf = BytesIO(png.tobytes())

# Create A4 landscape PDF and place at exact mm size
page_w_pt, page_h_pt = landscape(A4)  # 297mm x 210mm
c = canvas.Canvas(OUT_PDF, pagesize=(page_w_pt, page_h_pt))

x_pt = (297*mm - board_w_mm*mm) / 2.0
y_pt = (210*mm - board_h_mm*mm) / 2.0

c.drawImage(ImageReader(buf), x_pt, y_pt,
            width=board_w_mm*mm, height=board_h_mm*mm)

c.showPage()
c.save()

print("Saved:", OUT_PDF)