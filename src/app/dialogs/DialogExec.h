#pragma once

class QDialog;

// Ejecuta un diálogo con vista previa en el lienzo sin marcarlo como modal: KWin oscurece la
// ventana padre de los modales y taparía el resultado.
int execWithoutDimming(QDialog& dialog);
