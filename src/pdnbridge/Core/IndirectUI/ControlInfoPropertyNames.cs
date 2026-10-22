/////////////////////////////////////////////////////////////////////////////////
// Paint.NET                                                                   //
// Copyright (C) Rick Brewster, Tom Jackson, and past contributors.            //
// Portions Copyright (C) Microsoft Corporation. All Rights Reserved.          //
// See src/Resources/Files/License.txt for full licensing and attribution      //
// details.                                                                    //
// .                                                                           //
/////////////////////////////////////////////////////////////////////////////////

using System;
using System.Collections.Generic;
using System.Text;

namespace PaintDotNet.IndirectUI
{
    public enum ControlInfoPropertyNames
    {
        DisplayName = 0,
        Description = 1,
        ControlType = 2,
        ButtonText = 3,
        UseExponentialScale = 4,
        DecimalPlaces = 5,
        SliderSmallChange = 6,
        SliderSmallChangeX = 7,
        SliderSmallChangeY = 8,
        SliderLargeChange = 9,
        SliderLargeChangeX = 10,
        SliderLargeChangeY = 11,
        UpDownIncrement = 12,
        UpDownIncrementX = 13,
        UpDownIncrementY = 14,
        StaticImageUnderlay = 15,
        Multiline = 16,
        ShowResetButton = 17,
        SliderShowTickMarks = 18,
        SliderShowTickMarksX = 19,
        SliderShowTickMarksY = 20,

        WindowTitle = 21,
        WindowWidthScale = 22,
        WindowIsSizable = 23,

        // Kaint: añadidos en Paint.NET 4.x y 5.x, con los valores de la
        // documentación pública de la API (paintdotnet.github.io/apidocs).
        SliderSmallChangeZ = 24,
        SliderLargeChangeZ = 25,
        UpDownIncrementZ = 26,
        SliderShowTickMarksZ = 27,
        Footnote = 28,
        RangeWraps = 29,
        ControlStyle = 30,
        ControlColors = 31,
        WindowHelpContentType = 32,
        WindowHelpContent = 33,
        AllowAllFiles = 34,
        FileTypes = 35,
        ShowHeaderLine = 36,
        WindowShowBottomSeparatorLine = 37,
        ExponentialScale = 38,
        AutoPopulateFileTypes = 39
    }

    // Kaint: estilo del deslizador (ControlInfoPropertyNames.ControlStyle), Paint.NET 4.x.
    public enum SliderControlStyle
    {
        Default = 0,
        Hue = 1,
        HueCentered = 2,
        SaturationHue = 3
    }

    // Kaint: Paint.NET 4.x.
    public enum WindowHelpContentType
    {
        None = 0,
        PlainText = 1,
        RichText = 2,
        CustomViaCallback = 3
    }
}
