package com.exelite.launcher

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import com.exelite.engine.AxisCode
import com.exelite.engine.EngineManager
import com.exelite.engine.GamepadButton
import com.winlator.xserver.Pointer
import com.winlator.xserver.XServer
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.sin

/**
 * GamepadOverlay — İki modda çalışır:
 *
 *  GAMEPAD modu (varsayılan):
 *    Sol joystick + A/B/X/Y butonları → Wine XInput sinyalleri
 *
 *  MOUSE modu (setup wizard için):
 *    Tüm ekran dokunmatik mouse haline gelir.
 *    - Tek parmak sürükleme → mouse hareketi
 *    - Tek dokunuş (≤200ms) → sol mouse click
 *    - İki parmak dokunuş → sağ mouse click
 *    - Sağ alt köşede mod toggle butonu (G/M)
 */
class GamepadOverlay @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    // ── Genel ───────────────────────────────────────────────────
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    var engineManager: EngineManager? = null
    var xServer: XServer? = null  // XServer referansı — mouse/keyboard girdi enjeksiyonu için

    // ── Mod ─────────────────────────────────────────────────────
    enum class InputMode { GAMEPAD, MOUSE }
    var inputMode: InputMode = InputMode.GAMEPAD
        set(value) {
            field = value
            invalidate()
        }

    // ── Gamepad — Joystick ──────────────────────────────────────
    private var stickBaseX    = 250f
    private var stickBaseY    = 0f
    private val stickRadius   = 120f
    private val thumbRadius   = 50f
    private var stickCurrentX = stickBaseX
    private var stickCurrentY = 0f
    private var isStickActive = false
    private var stickPointerId = -1

    // ── Gamepad — Butonlar ──────────────────────────────────────
    private val buttons = mutableListOf<VirtualButton>()
    private val buttonRadius = 60f

    data class VirtualButton(
        val id: Int, var cx: Float, var cy: Float,
        val radius: Float, val label: String, val color: Int,
        var isPressed: Boolean = false, var pointerId: Int = -1
    )

    // ── Mouse modu — durum ──────────────────────────────────────
    private var mouseTouchX     = 0f
    private var mouseTouchY     = 0f
    private var mouseDownTime   = 0L
    private var mouseMoved      = false
    private val CLICK_MAX_MS    = 200L
    private val CLICK_MOVE_SLOP = 15f

    // ── Mod toggle butonu (ekranın sağ altı) ────────────────────
    private val toggleRect  = RectF()
    private val TOGGLE_SIZE = 80f

    init {
        buttons += VirtualButton(GamepadButton.A, 0f, 0f, buttonRadius, "A", Color.parseColor("#4CAF50"))
        buttons += VirtualButton(GamepadButton.B, 0f, 0f, buttonRadius, "B", Color.parseColor("#F44336"))
        buttons += VirtualButton(GamepadButton.X, 0f, 0f, buttonRadius, "X", Color.parseColor("#2196F3"))
        buttons += VirtualButton(GamepadButton.Y, 0f, 0f, buttonRadius, "Y", Color.parseColor("#FFEB3B"))
    }

    // ── Layout ──────────────────────────────────────────────────

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)

        stickBaseY    = h - 250f
        stickCurrentX = stickBaseX
        stickCurrentY = stickBaseY

        val bx = w - 250f; val by = h - 250f; val off = 140f
        buttons.find { it.id == GamepadButton.A }?.apply { cx = bx;       cy = by + off }
        buttons.find { it.id == GamepadButton.B }?.apply { cx = bx + off; cy = by       }
        buttons.find { it.id == GamepadButton.X }?.apply { cx = bx - off; cy = by       }
        buttons.find { it.id == GamepadButton.Y }?.apply { cx = bx;       cy = by - off }

        // Toggle butonu — sağ üst köşe
        toggleRect.set(w - TOGGLE_SIZE - 12f, 12f, w - 12f, 12f + TOGGLE_SIZE)
    }

    // ── Çizim ───────────────────────────────────────────────────

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        drawToggleButton(canvas)
        when (inputMode) {
            InputMode.GAMEPAD -> drawGamepad(canvas)
            InputMode.MOUSE   -> drawMouseCursor(canvas)
        }
    }

    private fun drawToggleButton(canvas: Canvas) {
        paint.style = Paint.Style.FILL
        paint.color = if (inputMode == InputMode.MOUSE) 0xCC3B5BFF.toInt() else 0x88223355.toInt()
        paint.alpha = 200
        canvas.drawRoundRect(toggleRect, 12f, 12f, paint)

        paint.color = Color.WHITE
        paint.alpha = 230
        paint.textSize = 22f
        paint.textAlign = Paint.Align.CENTER
        val label = if (inputMode == InputMode.MOUSE) "M" else "G"
        val cy = toggleRect.centerY() - (paint.descent() + paint.ascent()) / 2
        canvas.drawText(label, toggleRect.centerX(), cy, paint)

        // Alt etiket
        paint.textSize = 9f
        paint.alpha = 150
        val sub = if (inputMode == InputMode.MOUSE) "Mouse" else "Pad"
        canvas.drawText(sub, toggleRect.centerX(), toggleRect.bottom - 6f, paint)
    }

    private fun drawGamepad(canvas: Canvas) {
        // Joystick base
        paint.style = Paint.Style.FILL
        paint.color = 0x40FFFFFF
        paint.alpha = 64
        canvas.drawCircle(stickBaseX, stickBaseY, stickRadius, paint)
        // Thumb
        paint.color = 0xBBFFFFFF.toInt()
        paint.alpha = 187
        canvas.drawCircle(stickCurrentX, stickCurrentY, thumbRadius, paint)

        // Butonlar
        for (btn in buttons) {
            paint.alpha = if (btn.isPressed) 200 else 100
            paint.color = if (btn.isPressed) Color.WHITE else btn.color
            canvas.drawCircle(btn.cx, btn.cy, btn.radius, paint)
            paint.color = Color.WHITE; paint.alpha = 255
            paint.textSize = btn.radius; paint.textAlign = Paint.Align.CENTER
            val to = -(paint.descent() + paint.ascent()) / 2
            canvas.drawText(btn.label, btn.cx, btn.cy + to, paint)
        }
    }

    private fun drawMouseCursor(canvas: Canvas) {
        // Yarı saydam sinyal — "mouse modu aktif"
        paint.style = Paint.Style.STROKE
        paint.color = 0x443B5BFF.toInt()
        paint.strokeWidth = 1.5f
        // Ekran köşelerini hafif çerçevele
        val pad = 20f
        canvas.drawRoundRect(pad, pad, width - pad.toFloat(), height - pad.toFloat(), 24f, 24f, paint)

        // Hint metni
        paint.style = Paint.Style.FILL
        paint.color = 0x663B5BFF.toInt()
        paint.textSize = 18f
        paint.textAlign = Paint.Align.CENTER
        canvas.drawText("Mouse Mode — Tap to click, drag to move", width / 2f, height - 30f, paint)
    }

    // ── Touch ────────────────────────────────────────────────────

    override fun onTouchEvent(event: MotionEvent): Boolean {
        val idx = event.actionIndex
        val pid = event.getPointerId(idx)
        val x   = event.getX(idx)
        val y   = event.getY(idx)

        // Toggle butonu her zaman çalışır
        if (event.actionMasked == MotionEvent.ACTION_DOWN && toggleRect.contains(x, y)) {
            inputMode = if (inputMode == InputMode.GAMEPAD) InputMode.MOUSE else InputMode.GAMEPAD
            return true
        }

        return when (inputMode) {
            InputMode.GAMEPAD -> handleGamepad(event)
            InputMode.MOUSE   -> handleMouse(event)
        }
    }

    // ── Gamepad dokunuş ─────────────────────────────────────────

    private fun handleGamepad(event: MotionEvent): Boolean {
        val action = event.actionMasked
        val idx    = event.actionIndex
        val pid    = event.getPointerId(idx)
        val x      = event.getX(idx)
        val y      = event.getY(idx)

        when (action) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                if (!isStickActive && hypot(x - stickBaseX, y - stickBaseY) <= stickRadius * 1.5f) {
                    isStickActive = true; stickPointerId = pid; updateJoystick(x, y)
                } else {
                    buttons.firstOrNull { !it.isPressed && hypot(x - it.cx, y - it.cy) <= it.radius * 1.5f }
                        ?.apply { isPressed = true; pointerId = pid; engineManager?.sendButton(id, true); invalidate() }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) {
                    if (isStickActive && event.getPointerId(i) == stickPointerId)
                        updateJoystick(event.getX(i), event.getY(i))
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                if (isStickActive && stickPointerId == pid) {
                    isStickActive = false; stickPointerId = -1
                    stickCurrentX = stickBaseX; stickCurrentY = stickBaseY
                    engineManager?.sendAxis(AxisCode.LEFT_X, 0f)
                    engineManager?.sendAxis(AxisCode.LEFT_Y, 0f)
                    invalidate()
                } else {
                    buttons.firstOrNull { it.isPressed && it.pointerId == pid }
                        ?.apply { isPressed = false; pointerId = -1; engineManager?.sendButton(id, false); invalidate() }
                }
            }
        }
        return true
    }

    private fun updateJoystick(x: Float, y: Float) {
        val dx = x - stickBaseX; val dy = y - stickBaseY; val dist = hypot(dx, dy)
        if (dist <= stickRadius) {
            stickCurrentX = x; stickCurrentY = y
        } else {
            val a = atan2(dy, dx)
            stickCurrentX = stickBaseX + cos(a) * stickRadius
            stickCurrentY = stickBaseY + sin(a) * stickRadius
        }
        engineManager?.sendAxis(AxisCode.LEFT_X, (stickCurrentX - stickBaseX) / stickRadius)
        engineManager?.sendAxis(AxisCode.LEFT_Y, (stickCurrentY - stickBaseY) / stickRadius)
        invalidate()
    }

    // ── Mouse dokunuş ────────────────────────────────────────────
    private var lastMouseX = 0f
    private var lastMouseY = 0f
    private var touchDownX = 0f
    private var touchDownY = 0f
    private var subpixelDx = 0f
    private var subpixelDy = 0f

    private fun handleMouse(event: MotionEvent): Boolean {
        val x = event.x
        val y = event.y

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastMouseX = x
                lastMouseY = y
                touchDownX = x
                touchDownY = y
                subpixelDx = 0f
                subpixelDy = 0f
                mouseDownTime = System.currentTimeMillis()
                mouseMoved = false

                // İki parmak = sağ tık
                if (event.pointerCount >= 2) {
                    xServer?.let { server ->
                        server.injectPointerButtonPress(Pointer.Button.BUTTON_RIGHT)
                        server.injectPointerButtonRelease(Pointer.Button.BUTTON_RIGHT)
                    }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                val dx = x - lastMouseX
                val dy = y - lastMouseY

                val totalMove = hypot(x - touchDownX, y - touchDownY)
                if (totalMove > CLICK_MOVE_SLOP) {
                    mouseMoved = true
                }

                subpixelDx += dx
                subpixelDy += dy

                val intDx = subpixelDx.toInt()
                val intDy = subpixelDy.toInt()

                if (intDx != 0 || intDy != 0) {
                    xServer?.injectPointerMoveDelta(intDx, intDy)
                    subpixelDx -= intDx
                    subpixelDy -= intDy
                }

                lastMouseX = x
                lastMouseY = y
            }
            MotionEvent.ACTION_UP -> {
                val elapsed = System.currentTimeMillis() - mouseDownTime
                val totalMove = hypot(x - touchDownX, y - touchDownY)
                if (!mouseMoved && totalMove <= CLICK_MOVE_SLOP && elapsed < CLICK_MAX_MS && event.pointerCount == 1) {
                    // Sol tık — XServer API üzerinden
                    xServer?.let { server ->
                        server.injectPointerButtonPress(Pointer.Button.BUTTON_LEFT)
                        server.injectPointerButtonRelease(Pointer.Button.BUTTON_LEFT)
                    }
                }
            }
            MotionEvent.ACTION_CANCEL -> {
                mouseMoved = false
            }
        }
        return true
    }
}
