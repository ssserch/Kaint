#pragma once

class QDialog;

// Ejecuta un diálogo con vista previa en el lienzo sin marcarlo como modal:
// KWin oscurece la ventana padre de los modales y taparía el resultado.
// La entrada al resto de la ventana se bloquea igual.
int execWithoutDimming(QDialog& dialog);
