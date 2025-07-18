close all
clear 
clc

files = 'C:\Users\sersmirn\Dropbox\ScenePlane\';

for y=0:4
    for x = 0:4    
        I = imread([files,'Color',num2str(y),num2str(x),'.png']);
        [Y, U, V] = rgb2yuv(I(:,:,1), I(:,:,2), I(:,:,3), 'YUV420_8', 'BT601_f');
        Y = {Y};
        U = {U};
        V = {V};
        yuv_export(Y, U, V, ['Color',num2str(y),num2str(x),'.yuv'], 1);
    end
end