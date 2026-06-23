declare function ComputeColor(c as unsigned integer,chanel as unsigned integer) as unsigned integer
declare function sqrt(num as single) as single
sub BackGround_Redraw(elem as UiElement ptr)
    if (elem->RedrawNeeded) then
         dim adiv as integer=2
        
        dim mx as unsigned integer
        dim my as unsigned integer
        mx=elem->Width \adiv
        my=elem->Height \adiv
        
        dim i as unsigned integer
        dim j as unsigned integer
        dim m as integer=systemConfig->BgPoints
        dim ptArray as unsigned integer ptr=mem_alloc(sizeof(unsigned integer)*(m*2))
        for i=0 to m-1
            ptArray[i*2]=NextRandomNumber(0,mx)
            ptArray[i*2+1]=NextRandomNumber(0,my)
        next i
       
        
       
        dim xsquare as integer
        dim ysquare as integer
        dim sum as unsigned integer
        
        dim minDistance as unsigned integer
        dim result as unsigned integer
        
        dim x as unsigned integer
        dim y as unsigned integer
      
        for y=0 to my-1
            
            for x=0 to mx-1
                
                minDistance=-1
                for i=0 to m-1
                
                    if (x>ptArray[i*2]) then 
                        xsquare=(x-ptArray[i*2])
                    else
                        xsquare=(ptArray[i*2]-x)
                    end if
                    xsquare=(xsquare*256)/mx
                    if (xsquare>128) then xsquare=256-xsquare
                    
                    if (y>y-ptArray[i*2+1]) then
                        ysquare=y-ptArray[i*2+1]
                    else
                        ysquare=ptArray[i*2+1]-y
                    end if
                    ysquare=(ysquare*256)/my
                    if (ysquare>128) then ysquare=256-ysquare
                    
                    sum=sqrt(xsquare*xsquare+ysquare*ysquare) 'shr 1
                    if (sum>255) then sum=255
                    if (sum<minDistance) then minDistance=sum
                next i
                result=minDistance
                var ccc=computeColor(SystemConfig->BackColor,result and 255)
                
                for i= 0 to adiv -1
                    for j=0 to adiv-1
                        elem->Buffer->_buffer[(y+(my*j))*elem->Width+(x+(mx*i))]=ccc
                    next j
                next i
                  
            next x
        next y
        
       ' var t1=@"abcdefghijkl"
       ' var t2=@"mnopqrstvwxyz"
       ' var t3=@"ABCDEFGHIJKL"
       ' var t4=@"MNOPQRSTUVWXYZ"
       ' var t5=@"1234567890*+/="
       ' var t6=@"$"
       ' var rr=7
    
       ' elem->Buffer->DrawText(t1,50,50,&hFFFFFFFF,@"SIMPAGAR",rr)
       ' elem->Buffer->DrawText(t2,50,150,&hFFFFFFFF,@"SIMPAGAR",rr)
       ' elem->Buffer->DrawText(t3,50,260,&hFFFFFFFF,@"SIMPAGAR",rr)
       ' elem->Buffer->DrawText(t4,50,360,&hFFFFFFFF,@"SIMPAGAR",rr)
       ' elem->Buffer->DrawText(t5,50,470,&hFFFFFFFF,@"SIMPAGAR",rr)
       ' elem->Buffer->DrawText(t6,50,570,&hFFFFFFFF,@"SIMPAGAR",rr)
        
        
        
       
        mem_free(ptArray)
        elem->RedrawNeeded=0
    end if
end sub

#Define BIAS -&h4c000
function sqrt naked(num as single) as single
       ' dim _ebx as double=num
       ' dim _eax as double=0
       ' while _eax<_ebx
       '     _ebx+=_eax
       '     _ebx=_ebx shr 1
       '     _eax=num/_ebx
       ' wend
       ' return _ebx
   asm
        mov eax, [esp+4]
        shr eax, 1
        add eax, (1 << 29) - (1 << 22)
        'add eax, BIAS
        mov [esp+4], eax
        fld DWORD PTR [esp+4]
        ret 4
    end asm
end function

function ComputeColor(c as unsigned integer,chanel as unsigned integer) as unsigned integer
    var minchanel=96
    var ratio=255/(255-minchanel)
    var cc=minchanel+(chanel/ratio)
    
    if cc>255 then cc=255
    var red=(((c and &hFF0000) shr 16)*cc) shr 8
    var green=(((c and &hFF00) shr 8)*cc) shr 8
    var blue=(((c and &hff)*cc)) shr 8
    return &hFF000000 or ((red and &hFF) shl 16) or ((green and &hFF) shl 8) or (blue and &hFF)
end function