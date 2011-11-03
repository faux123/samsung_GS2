/* Portrait */

// Portrait Right
extern short CodTblX_PRight_48[];
extern short CodTblY_PRight_48[];

// Portrait Left
extern short CodTblX_PLeft_48[];
extern short CodTblY_PLeft_48[];

/* Landscape 1 */

// Landscape 90 Right is same with Portrait Left
extern short *CodTblX_LRight_48;
extern short *CodTblY_LRight_48;

// Landscape 90 Left
extern short CodTblX_LLeft_48[];
extern short CodTblY_LLeft_48[];


/* Landscape 2 */

// Landscape 270 Right
extern short *CodTblX_LRight2_48;
extern short *CodTblY_LRight2_48;

// Landscape 270 Left
extern short *CodTblX_LLeft2_48;
extern short *CodTblY_LLeft2_48;



extern short CodTblX_CCW_LLeft_44[];
extern short CodTblY_CCW_LLeft_44[];

extern short CodTblX_CW_LRight_44[];
extern short CodTblY_CW_LRight_44[];

extern short CodTblX_PLeft_44[];
extern short CodTblY_PLeft_44[];

extern short CodTblX_Right_44[];
extern short CodTblY_Right_44[];


extern short *tableX[MAX_HAND][MAX_ROTATION];
extern short *tableY[MAX_HAND][MAX_ROTATION];

extern short *tableX_48[MAX_HAND][MAX_ROTATION];
extern short *tableY_48[MAX_HAND][MAX_ROTATION];

extern short tilt_offsetX[MAX_HAND][MAX_ROTATION];
extern short tilt_offsetY[MAX_HAND][MAX_ROTATION];

extern short tilt_offsetX_48[MAX_HAND][MAX_ROTATION];
extern short tilt_offsetY_48[MAX_HAND][MAX_ROTATION];

extern short origin_offset[2];
extern short origin_offset_48[2];